#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "csapp.h"
#include "parse_lsa.h"
#include "mydns.h"

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
int transmit(int readfd, int writefd, char *buf, int *count, double *totlen);
int interrelate(int serverfd, int clientfd, char *buf, int idling, double *totlen, int write);
void *proxy(void *vargp);

double alpha, rtt, totlen, rate = 0;
int bitrate[10] = {100};
struct sockaddr_in fakeaddr;
char req[MAXBUF]; // HTTP request (with no header)
char wwwip[MAXLINE] = {0}; // custom server IP
FILE *logfile;
char *proxyport;

int main(int argc, char *argv[]) {
	int listenfd, connfd;
	char port[MAXLINE];
	struct sockaddr clientaddr;
	socklen_t addrlen = sizeof(struct sockaddr);
	
	/* Check command line args */
	if (argc != 7 && argc != 8) {
		fprintf(stderr, "usage: %s <log> <alpha> <listen-port> <fake-ip> <dns-ip> <dns-port> [<www-ip>]\n", argv[0]);
		exit(1);
	}

	logfile = fopen(argv[1],"w+");

	alpha = atof(argv[2]);

	listenfd = open_listenfd(argv[3]);
	proxyport = argv[3];

	fakeaddr.sin_family = AF_INET;
	fakeaddr.sin_addr.s_addr = inet_addr(argv[4]);
	fakeaddr.sin_port = htons(0);

	if(argc == 8)
		strcpy(wwwip, argv[7]);

	init_mydns(argv[5], atoi(argv[6]), argv[4]);

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
	status->port = 8080;
	strcpy(status->line, line);
	// A complete header
	if (sscanf(line, "%s %[a-z]://%[^/]%s %s",
				status->method,
				status->scm,
				status->hostname,
				status->path,
				status->version) != 5) {
		// A simplified header
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
	// In a reverse proxy, "localhost" will be omitted in the HTTP request header
	if(status->hostname[0] == '/') {
		strcpy(status->path, status->hostname);
		strcpy(status->hostname, "video.pku.edu.cn");
		status->port = 8080;
	}
	return 0;
}


int send_request(rio_t *rio, char *buf, struct status_line *status, int serverfd, int clientfd) {
	int len;
	memset(req, 0, sizeof(req));
	len = snprintf(buf, MAXLINE, "%s %s %s\r\n" \
			"Connection: close\r\n",	// Do not keep TCP connection alive
			status->method,
			*status->path ? status->path : "/",
			status->version);
	// Send request header to server
	if ((len = rio_writen(serverfd, buf, len)) < 0)
		return len;
	// Send request body to server
	while (len != 2) {
		if ((len = rio_readlineb(rio, buf, MAXLINE)) < 0)
			return len;
		// Ignore original "Connection" and "Proxy-Connection" field
		if (memcmp(buf, "Proxy-Connection: ", 18) == 0 || memcmp(buf, "Connection: ", 12) == 0)
				continue;
		strcat(req, buf);
		if ((len = rio_writen(serverfd, buf, len)) < 0)
			return len;
	}
	// Send remaining content, normally "\r\n"
	if (rio->rio_cnt &&
			(len = rio_writen(serverfd, rio->rio_bufptr, rio->rio_cnt)) < 0)
		return len;
	// Finish recording the request
	strcat(req, "\r\n");
	return 20;
}

// Replay the request, but "status" has been modified
int send_fake_request(char *buf,struct status_line *status, int serverfd, int clientfd) {
	int len = snprintf(buf, MAXLINE, "%s %s %s\r\n" \
			"Connection: close\r\n",
			status->method,
			*status->path ? status->path : "/",
			status->version);

	if ((len = rio_writen(serverfd, buf, len)) < 0)
		return len;

	if ((len = rio_writen(serverfd, req, strlen(req))) < 0)
		return len;

	return 20;
}

// Sort bitrate[] in decreasing order
int comp(const void * elem1, const void * elem2) {
	int f = *((int*)elem1);
	int s = *((int*)elem2);
	if (f < s) return	1;
	if (f > s) return -1;
	return 0;
}

// Must use interrelate to accelarate I/O
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

int interrelate(int serverfd, int clientfd, char *buf, int idling, double *totlen, int write) {
	int count = 0;
	int nfds = (serverfd > clientfd ? serverfd : clientfd) + 1;
	int flag;
	fd_set rlist;
	FD_ZERO(&rlist);

	while (1) {
		count++;

		FD_SET(clientfd, &rlist);
		FD_SET(serverfd, &rlist);

		struct timeval timeout = {2L, 0L};
		if ((flag = select(nfds, &rlist, NULL, NULL, &timeout)) < 0)
			return flag;
		if (flag) {
			if (FD_ISSET(serverfd, &rlist) &&
					((flag = transmit(serverfd, clientfd, buf, &count, totlen)) < 0))
				return flag;
			if (flag == 0)
				break;
			if (FD_ISSET(clientfd, &rlist) &&
					((flag = transmit(clientfd, serverfd, buf, &count, totlen)) < 0))
				return flag;
			if (flag == 0)
				break;
		}
		if (count >= idling)
			break;
	}
	return 0;
}

// Originated from "open_clientfd" in csapp.c
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
		
		// Special: bind this clientfd to our fake IP
		bind(clientfd, (SA *)&fakeaddr, sizeof(SA));
		
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

	rio_t rio;
	rio_readinitb(&rio, clientfd);

	struct status_line status;

	char buf[MAXLINE];
	char oldpath[MAXLINE]; // Remember the original path, if we are going to send a modified path
	char *tmp1, *tmp2, tmp3[MAXLINE];
	int flag;
	struct timeval start, end;
	int chosen_bitrate;

	totlen = 0;

	if ((flag = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
		gettimeofday(&start, NULL);

		if(parseline(buf, &status) < 0)
			return NULL;

		int is_f4m = strstr(status.path, ".f4m") ? 1 : 0;
		int is_video = strstr(status.path, "Seg") ? 1 : 0;

		// If we are requesting a video chunk,
		// choose a bitrate, and modify "status.path".
		if(is_video && bitrate && rate > 0) {
			for(int i = 0; i < 10; i++) {
				if(bitrate[i] <= rate/1.5) {
printf("OLD PATH: %s\n", status.path);
					tmp1 = strstr(status.path, "vod/") + 4;
					tmp2 = strstr(status.path, "Seg");
					*tmp1 = 0;
					snprintf(tmp3, MAXLINE, "%s%d%s", status.path, bitrate[i], tmp2);
					strcpy(status.path, tmp3);
					chosen_bitrate = bitrate[i];
printf("NEW PATH: %s\n", status.path);
					break;
				}
			}
		}

		// If we are requesting an f4m file, modify "status.path".
		if(is_f4m) {
printf("OLD PATH: %s\n", status.path);
			strcpy(oldpath, status.path);
			tmp1 = strstr(status.path, ".f4m");
			strcpy(tmp1, "_nolist.f4m");
printf("NEW PATH: %s\n", status.path);
		}

printf("%s\n", status.path);
		sprintf(tmp3, "%d", status.port);

		// Use custom www-ip given in the command line, or use DNS service.
		if(strlen(wwwip) != 0)
			strcpy(status.hostname, wwwip);
		else if(!strcmp(status.hostname, "video.pku.edu.cn")) {
			struct addrinfo * result;
			resolve("video.pku.edu.cn", "8080", NULL, &result);
			strcpy(status.hostname, inet_ntoa(((struct sockaddr_in *)result->ai_addr)->sin_addr));
		}
		// Establish a connection to the server.
		if((serverfd = open_clientfd2(status.hostname, tmp3)) < 0) {
			return NULL;
		}
		// Send HTTP request (might have been modified).
		if((flag = send_request(&rio, buf, &status, serverfd, clientfd)) < 0) {
			return NULL;
		}
		// Send response back using I/O multiplexing.
		if (interrelate(serverfd, clientfd, buf, flag, &totlen, 0) < 0) {
			return NULL;
		}
		// If we are requesting an f4m file, fetch the one with bitrates
		// but do not send it back to the client.
		if(is_f4m) {
			strcpy(status.path, oldpath);
			if((serverfd2 = open_clientfd2(status.hostname, tmp3)) < 0)	
				return NULL;

			if((flag = send_fake_request(buf, &status, serverfd2, clientfd)) < 0)
				return NULL;

			// Discover available bitrates.
			tmp1 = buf;
			int i = 0;
			read(serverfd2, buf, MAXBUF);
			for(int i = 0; tmp1 = strstr(tmp1, "bitrate=\""); i++) {
				tmp1 += 9;
				tmp2 = strchr(tmp1, '\"');
				*tmp2 = 0;
				bitrate[i] = atoi(tmp1);
				*tmp2 = '\"';
printf("Discover bitrate: %d\n", bitrate[i]);
			}
			qsort(bitrate, 10, sizeof(int), comp);
			close(serverfd2);
		}

		// Update statistics.
		gettimeofday(&end, NULL);
		rtt = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec)/(double)1000000;
		if(totlen > 0)
			rate = rate*(1-alpha) + 8*alpha*totlen/rtt/1000;

		fprintf(logfile, "%d %6f %6f %6f %d %s %s\n",
						(int)time(NULL), rtt, 8*totlen/rtt/1000, rate, chosen_bitrate, status.hostname, status.path);

		close(serverfd);
	}
	close(clientfd);
	return NULL;
}
