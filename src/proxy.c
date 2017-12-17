#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "csapp.h"

struct status_line {
	char line[MAXLINE];
	char method[20];
	char scm[20];
	char hostname[MAXLINE];
	int	port;
	char path[MAXLINE];
	char version[20];
};

int parseline(char *line, struct status_line *status);
int send_request(rio_t *rio, char *buf,
		struct status_line *status, int serverfd, int clientfd);
int transmit(int readfd, int writefd, char *buf, int *count, double *totlen, int write);
int interrelate(int serverfd, int clientfd, char *buf, int idling, double *totlen, int write);
void *proxy(void *vargp);

double alpha;
int bitrate[10] = {100};
double rtt, totlen, rate = 0;
struct sockaddr_in fake_addr;

int main(int argc, char *argv[]) {
	int listenfd, connfd;
	char hostname[MAXLINE], port[MAXLINE];
	struct sockaddr clientaddr;
	socklen_t addrlen = sizeof(struct sockaddr);
	
	/* Check command line args */
	if (argc != 7 && argc != 8) {
		fprintf(stderr, "usage: %s <log> <alpha> <listen-port> <fake-ip> <dns-ip> <dns-port> [<www-ip>]\n", argv[0]);
		exit(1);
	}

	alpha = atof(argv[2]);

	if(argc == 8) ;

	fake_addr.sin_family = AF_INET;
	fake_addr.sin_port = htons(0);
	fake_addr.sin_addr.s_addr = inet_addr(argv[4]);

	listenfd = open_listenfd(argv[3]);

	while (1) {
		int *clientfd = (int *)malloc(sizeof(int));
		do {
			*clientfd = accept(listenfd, &clientaddr, &addrlen);
		}
		while(*clientfd < 0);

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

char buf2[MAXBUF];
int send_request(rio_t *rio, char *buf, struct status_line *status, int serverfd, int clientfd) {
	int len;
	memset(buf2, 0, sizeof(buf2));
	len = snprintf(buf, MAXLINE, "%s %s %s\r\n",
			status->method,
			*status->path ? status->path : "/",
			status->version);

	if ((len = rio_writen(serverfd, buf, len)) < 0)
		return len;

	while (len != 2) {
		if ((len = rio_readlineb(rio, buf, MAXLINE)) < 0)
			return len;
		strcat(buf2, buf);
		if ((len = rio_writen(serverfd, buf, len)) < 0)
			return len;
	}

	if (rio->rio_cnt &&
			(len = rio_writen(serverfd, rio->rio_bufptr, rio->rio_cnt)) < 0)
		return len;

	strcat(buf2, "\r\n");
	return 20;
}

int send_fake_request(char *buf,struct status_line *status, int serverfd, int clientfd) {
	int len = snprintf(buf, MAXLINE, "%s %s %s\r\n",
			status->method,
			*status->path ? status->path : "/",
			status->version);

	if ((len = rio_writen(serverfd, buf, len)) < 0)
		return len;

	if ((len = rio_writen(serverfd, buf2, strlen(buf2))) < 0)
		return len;

	return 20;
}

int comp(const void * elem1, const void * elem2) {
	int f = *((int*)elem1);
	int s = *((int*)elem2);
	if (f < s) return	1;
	if (f > s) return -1;
	return 0;
}

void get_bitrate(char *buf, int *bitrate) {
	char *tmp1 = buf, *tmp2;
	int i = 0;
	while(tmp1 = strstr(tmp1, "bitrate=\"")) {
		tmp1 += 9;
		tmp2 = strchr(tmp1, '\"');
		*tmp2 = 0;
		bitrate[i++] = atoi(tmp1);
		*tmp2 = '\"';
printf("Discover bitrate: %d\n", bitrate[i-1]);
	}
	qsort(bitrate, 10, sizeof(int), comp);
}

int transmit(int readfd, int writefd, char *buf, int *count, double *totlen, int write) {
//printf("enter transmit, write = %d\n", write);
	int len = 0;
	if ((len = read(readfd, buf, MAXBUF)) > 0) {
		*count = 0;
		if(write) {
			len = rio_writen(writefd, buf, len);
			if(totlen)
				*totlen += len;
		}
		else
			get_bitrate(buf, bitrate);
	}
	return len;
}

int interrelate(int serverfd, int clientfd, char *buf, int idling, double *totlen, int write) {
//printf("enter interrelate, write = %d\n", write);
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
					((flag = transmit(serverfd, clientfd, buf, &count, totlen, write)) < 0))
				return flag;
			if (flag == 0)
				break;
			if (FD_ISSET(clientfd, &rlist) &&
					((flag = transmit(clientfd, serverfd, buf, &count, NULL, 1)) < 0))
				return flag;
			if (flag == 0)
				break;
		}
		if (count >= idling)
			break;
	}
	return 0;
}
int open_clientfd2(char *hostname, char *port) {
	int clientfd, rc;
	struct addrinfo hints, *listp, *p;

	/* Get a list of potential server addresses */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;	/* Open a connection */
	hints.ai_flags = AI_NUMERICSERV;	/* ... using a numeric port arg. */
	hints.ai_flags |= AI_ADDRCONFIG;	/* Recommended for connections */
	if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0) {
		fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, port, gai_strerror(rc));
		return -2;
	}

	/* Walk the list for one that we can successfully connect to */
	for (p = listp; p; p = p->ai_next) {
		/* Create a socket descriptor */
		if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
			continue; /* Socket failed, try the next */
		bind(clientfd, (SA *)&fake_addr, sizeof(SA));
		/* Connect to the server */
		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
			break; /* Success */
		if (close(clientfd) < 0) { /* Connect failed, try another */	//line:netp:openclientfd:closefd
			fprintf(stderr, "open_clientfd: close failed: %s\n", strerror(errno));
			return -1;
		}
	}

	/* Clean up */
	freeaddrinfo(listp);
	if (!p) /* All connects failed */
		return -1;
	else		/* The last connect succeeded */
		return clientfd;
}
void *proxy(void *vargp) {
	Pthread_detach(Pthread_self());

	int serverfd, serverfd2;
	int clientfd = *(int *)vargp;
	free(vargp);
totlen = 0;
	rio_t rio;
	rio_readinitb(&rio, clientfd);

	struct status_line status;

	char buf[MAXLINE], tmp[MAXLINE];
	int flag;
	struct timeval start, end;

	if ((flag = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
		gettimeofday(&start, NULL);

		if(parseline(buf, &status) < 0)
			return NULL;

		int is_f4m = strstr(status.path, ".f4m") ? 1 : 0;
		int is_video = strstr(status.path, "Seg") ? 1 : 0;
		if(is_video && bitrate && rate > 0) {
			for(int i = 0; i < 10; i++) {
				if(bitrate[i] <= rate/1.5) {
printf("OLD PATH: %s\n", status.path);
					char *tmp1 = strstr(status.path, "vod/") + 4;
					char *tmp2 = strstr(status.path, "Seg");
					*tmp1 = 0;
					char tmp3[MAXLINE];
					snprintf(tmp3, MAXLINE, "%s%d%s", status.path, bitrate[i], tmp2);
					strcpy(status.path, tmp3);
printf("NEW PATH: %s\n", status.path);
					break;
				}
			}
		}

		char oldpath[MAXLINE];
		if(is_f4m) {
printf("OLD PATH: %s\n", status.path);
			strcpy(oldpath, status.path);
			char *tmp = strstr(status.path, ".f4m");
			strcpy(tmp, "_nolist.f4m");
printf("NEW PATH: %s\n", status.path);
		}

printf("%s\n", status.path);
		sprintf(tmp, "%d", status.port);

		if((serverfd = open_clientfd2(status.hostname, tmp)) < 0)
			return NULL;

		if((flag = send_request(&rio, buf, &status, serverfd, clientfd)) < 0)
			return NULL;

		if (interrelate(serverfd, clientfd, buf, flag, &totlen, 1) < 0)
			return NULL;

		if(is_f4m) {
			strcpy(status.path, oldpath);
			if((serverfd2 = open_clientfd2(status.hostname, tmp)) < 0)	
				return NULL;

			if((flag = send_fake_request(buf, &status, serverfd2, clientfd)) < 0)
				return NULL;

			if (interrelate(serverfd2, clientfd, buf, flag, &totlen, 0) < 0) 
				return NULL;

			close(serverfd2);
		}

		gettimeofday(&end, NULL);
		rtt = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec)/(double)1000000;
		if(totlen > 0)
			rate = rate*(1-alpha) + 8*alpha*totlen/rtt/1000;

		close(serverfd);
	}
	close(clientfd);
	return NULL;
}
