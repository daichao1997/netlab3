#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mydns.h"
#include "parse_lsa.h"

/* DEFINES HERE*/
#define ARGC_USERR			7
#define ARGC_USELS			6
#define ATON(str, inet)		inet_aton(str, inet)
typedef int (*ip_getter_t )( char *, char *,struct in_addr **);

struct addr_list_t
{
	char * addr;
	struct in_addr iaddr;
	struct addr_list_t * next;
};

struct addr_node_map
{
	struct addr_list_t * serverAddr;
	struct node_t * pnode;
	struct addr_node_map * next;
};



/*GLOBAL VARIABLES HERE*/
static char CORRECT_TARGET[30] = "video.pku.edu.cn";
static enum algo_t{ROUND_ROBIN,LSA} algo;
static struct addr_list_t * addrList;		//it is header
static struct addr_node_map * addrMap;		//it is header 


/*FUNCTION HERE*/
void Usage();
void send_error( char * format, ...);
int open_listenfd(char *port,char * ip_addr);

int naive_getip( char * targetaddr, char * srcaddr,
				struct in_addr ** pinaddrref);
int rr_getip( char * targetaddr,  char * srcaddr,
			struct in_addr ** pinaddrref);
int ls_getip( char * addr,  char * srcaddr,
			struct in_addr ** pinaddrref);

void read_servers_file(FILE * file);	//and fill addrList
void read_lsa_file(FILE * file);		//need dijkstra...

void free_addr_list(struct addr_list_t *head);		//list operation
void print_addrList(struct addr_list_t *head);		//for debug



/*IMPLEMENTATIONS HERE*/

void Usage()
{
	fprintf(stderr,"Usage : ./nameserver [-r] <log> <ip> <port> <servers> <LSAs>\n");
	exit(-1);
}


void send_error( char * format, ...)
{
	va_list va;
	va_start(va,format);
	vfprintf(stderr, format,va);
	va_end(va);
	exit(-1);
}

int open_listenfd(char * port, char * ip_addr)
{
	int listenfd;
	struct sockaddr_in hint;
	if((listenfd = socket(AF_INET,SOCK_DGRAM,0)) == -1)
	{
		fprintf(stderr,"server cannot create socket\n");
		return -1;
	}

	bzero(&hint,sizeof(struct sockaddr_in));
	ATON(ip_addr, &hint.sin_addr);	
	hint.sin_port = htons(atoi(port));
	hint.sin_family = AF_INET;
	//hint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//(hint.sin_addr.s_addr);
	if(bind(listenfd,(struct sockaddr *)&hint,sizeof(struct sockaddr_in)) == -1) 
	{
		fprintf(stderr,"cannot bind server addr\n");
		close(listenfd);
		return -1;
	}
	return listenfd;
}

int open_listenfd_bak(char *port,struct sockaddr* ai_addr, size_t addrlen)
{

	struct addrinfo hints, *listp,*p;
	int listenfd, optval=1;

	/*GET ADDR*/
	memset(&hints,0,sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_PASSIVE ;
	hints.ai_flags = AI_NUMERICSERV;
	getaddrinfo(NULL,port,&hints,&listp);

	/*Looking for one which an be binded to*/
	for(p = listp;p;p=p->ai_next)
	{
		//get an socket
		if((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
			continue;

		//set opt
		setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,
					( void*) &optval, sizeof(int));

		//bind
		if(bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
			break;		//bind success
		close(listenfd);
	}

	freeaddrinfo(listp);
	if(!p) return -1;	//no listp

	return listenfd;
}


// IMPLEMENTATION OF ip_getter_t


int is_addr_correct( char * targetaddr)
{
	int ret = strcmp(targetaddr,CORRECT_TARGET);
	fprintf(stderr,"targetaddr = %s, CORRECT_TARGET = %s, compare result = %d\n",targetaddr,CORRECT_TARGET,ret);
	return (ret == 0);
}

int naive_getip( char * addr,  char * srcaddr, struct in_addr ** pinaddrref)
{
	if(addrList == NULL) return -1;
	if(!is_addr_correct(addr)) return -1;
	(*pinaddrref) = &(addrList->next->iaddr);
	return 0;
}


static struct addr_list_t * curr_round_robin = NULL;
			//get initialized in function read_servers_file(f)
int rr_getip( char * addr,  char * srcaddr, struct in_addr ** pinaddrref)
{
	if(!curr_round_robin)
		return -1;
	if(!is_addr_correct(addr)) return -1;
	*pinaddrref = &curr_round_robin->iaddr;//return curr
	curr_round_robin = curr_round_robin->next;	//iterate
	return 0;
}



int ls_getip( char * addr,  char * srcaddr,struct in_addr ** pinaddrref)
{
	if(addrList == NULL || !is_addr_correct(addr)) 
		return -1;
	struct node_t *psrc;
	int ret = find_name(srcaddr, &psrc);
	if(ret == -1) //addr not found
		return -1;
	
	struct addr_node_map * list = addrMap->next;
	int dis,mindis = LSA_INFINITY_DIS;
	struct in_addr * paddr;
	while(list)
	{
		dis = dijkstra(psrc, list->pnode);
		if(dis<mindis) 
		{
			paddr = &list->serverAddr->iaddr;
			mindis = dis;
		}
		list = list->next;
	}
	
	if(mindis == LSA_INFINITY_DIS)
	{
		fprintf(stderr,"cannot reach!\n");
		return -1;
	}
	*pinaddrref = paddr;

	return 0;

}


// IMPLEMENTATION OF FILE_READERS

/**
 * HELPER FUNCTION read_servers_file()
 * read servers file and initialize the list
 */
void read_servers_file(FILE * f)
{
	 size_t BUFSIZE = 20;
	char * buf = (char *)malloc(BUFSIZE);

	//inititalize the header of addrList
	addrList = (struct addr_list_t *)malloc(sizeof(struct addr_list_t));
	addrList->next = NULL;
	addrList->addr = NULL;
	
	//current pointer
	struct addr_list_t *curr;
	while(fgets(buf,BUFSIZE,f))
	{
		size_t ll=strlen(buf);
		buf[ll-1]='\0';
		//institiate an object of new addr_list_t
		curr = (struct addr_list_t *)malloc(sizeof(struct addr_list_t));
		curr->addr = buf;
		ATON(buf,&curr->iaddr);
		curr->next = addrList->next;	
		addrList->next = curr;			//insert it into list
		
		buf = (char *)malloc(BUFSIZE);	//malloc a new buffer
	}

	//go through the list, and make the last point to
	//the first
	
	curr = addrList->next;
	if(curr!=NULL)
	{
		while(curr->next) curr=curr->next;
		//now curr->next will be null
		curr->next = addrList->next;	//POINT TO FIRST
		curr_round_robin = addrList->next;	//initialize the round robin chain
	}

	
	free(buf);
}


void read_lsa_file(FILE * f)
{
	init_lsa(f);
	
	addrMap = (struct addr_node_map *)malloc(sizeof(struct addr_node_map));
	addrMap->pnode = NULL;
	addrMap->next = NULL;
	addrMap->serverAddr = NULL;
	//Initialize the addr_node_map
	struct addr_list_t * plist = addrList->next;
	struct addr_node_map * p;

	while(plist)
	{
		//fill new node
		p = (struct addr_node_map *) malloc(sizeof(struct addr_node_map));
		p->serverAddr = plist;
		find_name(plist->addr, &p->pnode);

		//insert new node
		p->next = addrMap->next;
		addrMap->next = p;

		plist = plist->next;
		if(plist == addrList->next) break;	//loop
	}
}

void free_addr_list(struct addr_list_t * list)
{
	struct addr_list_t * p;
	while(list)
	{
		p=list->next;
		if(list->addr)
			free(list->addr);
		free(list);
		list = p;
	}
}



void free_addr_map(struct addr_node_map * list)
{
	struct addr_node_map * p;
	while(list)
	{
		p=list->next;
		free(list);
		list = p;
	}
}


void print_addrList(struct addr_list_t *p)
{
	p = addrList->next;
	while(p)
	{
		fprintf(stderr,"{ addr = %s, iaddr = %x} -> \n",p->addr, p->iaddr.s_addr);
		p=p->next;
	}

}
/*MAIN ENTRY*/
int main(int argc, char * argv[])
{
	//INIT
	
	time_t start = time(NULL), end;

	int offset=0;
	FILE * logFile, *serverFile, *lsaFile;
	ip_getter_t getip = ls_getip;
	if(argc == ARGC_USERR){
	   	algo = ROUND_ROBIN; 
		offset = 1;
		getip = rr_getip;
	}
	else if (argc == ARGC_USELS) algo = LSA;
	else Usage();

	//TODO: SET ip_getter = round_robin or lsa

	//READ FILES....
	logFile = fopen(argv[1+offset],"w");
	if(!logFile)
		send_error("cannot open log file %s\n",argv[1+offset]);

	serverFile = fopen(argv[4+offset],"r");
	if(!serverFile)
		send_error("cannot open server file %s\n",argv[4+offset]);
	read_servers_file(serverFile);

	lsaFile = fopen(argv[5+offset],"r");
	if(!lsaFile)
		send_error("cannot open lsa file %s\n",argv[5+offset]);
	read_lsa_file(lsaFile);
	

	//LISTEN REQUESTS
	int listenfd = open_listenfd(argv[3+offset],argv[2+offset]);
	char packet[MAX_PACKET_SIZE], back_packet[MAX_PACKET_SIZE];
	ssize_t len;
	uint16_t req_num;
	struct sockaddr_in client;
	socklen_t client_len = sizeof(client);
	while(1)
	{
		len = recvfrom(listenfd,packet,MAX_PACKET_SIZE,0,
					(struct sockaddr *)&client, &client_len);

		end = time(NULL);
		struct DNS_header_t * pheader = (struct DNS_header_t *)packet;
		req_num = ntohs(pheader->QDcount);
		if(req_num == 0)
		{
			//TODO: log it?
			continue;
		}
		 char *dst[req_num];

		/*RESOLV REQUEST, GET WEB ADDR AND FILL IT INTO DST*/
		req_num = resolve_request_info(packet, &dst);
		if(req_num == 0)
		{
			//TODO: error occured.... log it?
			continue;
		}

		/*GET THE IP_ADDR AND SEND MUTIPLE PACKETS BACK*/
		struct in_addr *res_addrs[req_num];
		for(size_t i=0;i<req_num;i++)
		{
			int flag = RESPOND_TAG,ret;
			if((ret = (*getip)(dst[i], inet_ntoa(client.sin_addr),&res_addrs[i])) < 0)
				flag=RCODE3_TAG;

			/*FILL RESPOND PACKET*/
			size_t back_len = fill_response(1, dst+i, ntohs(pheader->ID), res_addrs+i,back_packet,flag);
			
			/* SEND RESPOND PACKET BACK*/
			sendto(listenfd,back_packet,back_len,0,
					(struct sockaddr *)&client,client_len);

			/*print log*/
			fprintf(logFile,"%ld %s %s ",
					end-start, inet_ntoa(client.sin_addr),
					dst[i]);
			if(flag != RCODE3_TAG)
				fprintf(logFile,"%s\n",inet_ntoa(*res_addrs[i]));
			else fprintf(logFile,"%s\n", "RCODE_3,NO SERVER FOUND!");

			//DEBUG
			//
			fprintf(stderr,"%ld %s %s ",
					end-start, inet_ntoa(client.sin_addr),
					dst[i]);
			if(flag != RCODE3_TAG)
				fprintf(stderr,"%s\n",inet_ntoa(*res_addrs[i]));
			else fprintf(stderr,"%s\n", "RCODE_3,NO SERVER FOUND!");
			fflush(logFile);
		}
		
		
	}

	//CLEAN UP
	fclose(logFile);
	fclose(serverFile);
	fclose(lsaFile);
	close(listenfd);
	free_addr_list(addrList);
	free_addr_map(addrMap);
	free_lsa_resources();
	return 0;
}
