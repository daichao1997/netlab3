#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "csapp.h"

#define log(func) fprintf(stderr, #func" error: %s\n%s%s:%d%s\n", \
		strerror(errno), \
		status.scm, \
		status.hostname, \
		status.port, \
		status.path)

struct status_line {
	char line[MAXLINE];
	char method[20];
	char scm[20];
	char hostname[MAXLINE];
	int  port;
	char path[MAXLINE];
	char version[20];
};

int parseline(char *line, struct status_line *status);
int send_request(rio_t *rio, char *buf,
		struct status_line *status, int serverfd, int clientfd);
int transmit(int readfd, int writefd, char *buf, int *count, double *totlen);
int interrelate(int serverfd, int clientfd, char *buf, int idling, double *totlen);
void *proxy(void *vargp);

double alpha;
int main(int argc, char *argv[]) {
	int listenfd, connfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	
	/* Check command line args */
	if (argc != 7 && argc != 8) {
		fprintf(stderr, "usage: %s <log> <alpha> <listen-port> <fake-ip> <dns-ip> <dns-port> [<www-ip>]\n", argv[0]);
		exit(1);
	}
	else {
		alpha = atof(argv[2]);
		if(argc == 8) ;
	}

	listenfd = Open_listenfd(argv[3]);

	while ("serve forever") {
		struct sockaddr clientaddr;
		socklen_t addrlen = sizeof clientaddr;
		int *clientfd = (int *)Malloc(sizeof(int));
		do *clientfd = accept(listenfd, &clientaddr, &addrlen);
		while (*clientfd < 0);

		pthread_t tid;
		Pthread_create(&tid, NULL, proxy, clientfd);
	}
}

int parseline(char *line, struct status_line *status) {
	status->port = 80;
	strcpy(status->line, line);

	if (sscanf(line, "%s %[a-z]://%[^/]%s %s",
				status->method,
				status->scm,
				status->hostname,
				status->path,
				status->version) != 5) {
		if (sscanf(line, "%s %s %s",
					status->method,
					status->hostname,
					status->version) != 3)
			return -1;
		*status->scm = *status->path = 0;
	} else
		strcat(status->scm, "://");

	char *pos = strchr(status->hostname, ':');
	if (pos) {
		*pos = 0;
		status->port = atoi(pos + 1);
	}
	return 0;
}

int send_request(rio_t *rio, char *buf,
		struct status_line *status, int serverfd, int clientfd) {
	int len;
	if (strcmp(status->method, "CONNECT")) {
		len = snprintf(buf, MAXLINE, "%s %s %s\r\n" \
				"Connection: close\r\n",
				status->method,
				*status->path ? status->path : "/",
				status->version);
		if ((len = rio_writen(serverfd, buf, len)) < 0)
			return len;
		while (len != 2) {
			if ((len = rio_readlineb(rio, buf, MAXLINE)) < 0)
				return len;
			if (memcmp(buf, "Proxy-Connection: ", 18) == 0 || memcmp(buf, "Connection: ", 12) == 0)
				continue;
			if ((len = rio_writen(serverfd, buf, len)) < 0)
				return len;
		}
		if (rio->rio_cnt &&
				(len = rio_writen(serverfd, rio->rio_bufptr, rio->rio_cnt)) < 0)
			return len;
		return 20;
	} else {
		len = snprintf(buf, MAXLINE, "%s 200 OK\r\n\r\n", status->version);
		if ((len = rio_writen(clientfd, buf, len)) < 0)
			return len;
		return 300;
	}
}

int transmit(int readfd, int writefd, char *buf, int *count, double *totlen) {
	int len = 0;
	if ((len = read(readfd, buf, MAXBUF)) > 0) {
		*count = 0;
		len = rio_writen(writefd, buf, len);
		if(totlen)
			*totlen += len;
	}
	return len;
}

int interrelate(int serverfd, int clientfd, char *buf, int idling, double *totlen) {
	int count = 0;
	int nfds = (serverfd > clientfd ? serverfd : clientfd) + 1;
	int flag;
	fd_set rlist, xlist;
	FD_ZERO(&rlist);
	FD_ZERO(&xlist);

	while (1) {
		count++;

		FD_SET(clientfd, &rlist);
		FD_SET(serverfd, &rlist);
		FD_SET(clientfd, &xlist);
		FD_SET(serverfd, &xlist);

		struct timeval timeout = {2L, 0L};
		if ((flag = select(nfds, &rlist, NULL, &xlist, &timeout)) < 0)
			return flag;
		if (flag) {
			if (FD_ISSET(serverfd, &xlist) || FD_ISSET(clientfd, &xlist))
				break;
			if (FD_ISSET(serverfd, &rlist) &&
					((flag = transmit(serverfd, clientfd, buf, &count, totlen)) < 0))
				return flag;
			if (flag == 0)
				break;
			if (FD_ISSET(clientfd, &rlist) &&
					((flag = transmit(clientfd, serverfd, buf, &count, NULL)) < 0))
				return flag;
			if (flag == 0)
				break;
		}
		if (count >= idling)
			break;
	}
	return 0;
}

void *proxy(void *vargp) {
	Pthread_detach(Pthread_self());

	int serverfd;
	int clientfd = *(int *)vargp;
	free(vargp);

	rio_t rio;
	rio_readinitb(&rio, clientfd);

	struct status_line status;

	char buf[MAXLINE], tmp[MAXLINE];
	int flag;
	struct timeval start, end;
	double rtt, totlen, rate = 0;

	if ((flag = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
		gettimeofday(&start, NULL);

		if(parseline(buf, &status) < 0) {
			fprintf(stderr, "parseline error: '%s'\n", buf);
			return NULL;
		}
		int is_f4m = strstr(status.path, ".f4m") ? 1 : 0;
		int is_video = strstr(status.path, "Seg") ? 1 : 0;
		if(is_video) {
			;
		}

printf("%s\n", status.line);
		sprintf(tmp, "%d", status.port);

		if((serverfd = open_clientfd(status.hostname, tmp)) < 0) {
			log(open_clientfd);
			return NULL;
		}

		if((flag = send_request(&rio, buf, &status, serverfd, clientfd)) < 0) {
			log(send_request);
			return NULL;
		}

		if (interrelate(serverfd, clientfd, buf, flag, &totlen) < 0) {
			log(interrelate);
			return NULL;
		}
		
		gettimeofday(&end, NULL);
		rtt = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec)/(double)1000000;
		if(rtt < 0) {
			printf("rtt < 0\n");
			exit(0);
		}
		if(totlen > 0) {
			rate = rate*(1-alpha) + alpha*totlen/rtt/1000;
printf("alpha: %f, totlen: %f, rtt: %f, rate: %f\n", alpha, totlen, rtt, rate);
		}
		close(serverfd);
	}
	close(clientfd);
	return NULL;
}
