#include "mydns.h"
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

static struct sockaddr_in dns_ip;
static struct sockaddr_in client_ip;
unsigned int dns_port;
static  size_t BUFFER_MAX_SIZE = 200;
static uint16_t transcation_id = 0;
/*
 * HELPER FUNCTION DEFINITIONS
 */
size_t addr2dns( char * addr, char * dst); 
size_t dns2addr( char * dns, char * addr);
int open_clientfd();




/*
 * =================================
 *			IMPLEMENTATIONS
 * =================================
 */

/**
 * HELPER FUNCTION addr2dns(addr,dst)
 *
 * convert an address (such as www.baidu.com) to fit the
 * dns packet format
 * 
 * @param addr	the specified address
 * @param dst	the destination, can be a buffer or the packet
 *
 * @returns	the size of the dns_string if successed
 *			else return 0 [ERROR OCCURED]
 */
size_t addr2dns( char * addr, char * dst)
{
	if(!addr) return 0;		//INVALID ADDRESS STRING
	if(!dst) return 0;		//INVALID DESTINATION BUFFER

	size_t paddr=0,pdst=0;
	u_char currlen=0;
	size_t length = strlen(addr);	//addr = "www.baidu.com0"
	memset(dst,0,length+2);			//dst = "000000000000000"
	strcpy(dst + 1, addr);	//dst = "0www.baidu.com0"
	while(paddr < length)
	{
		if (addr[paddr] == '.')
		{
			dst[pdst] = currlen;
			paddr++;
			pdst = paddr;
			currlen = 0;
		}
		else
		{
			currlen ++;
			paddr++;
		}
	}								//dst = "3www5baidu.com0"
	dst[pdst]=currlen;				//dst = "3www5baidu3com0"
	return length+2;
}

/**
 * HELPER FUNCTION dns2addr(dns,addr)
 * convert dns payload into address (such as www.baidu.com)
 *
 * @param dns	the dns packet, starts at the first bit of "QNAME"
 *				field
 * @param addr	the buffer to fill
 *
 * @returns the length of dns packet parsed
 */
size_t dns2addr( char * dns, char * addr)
{
	if(dns == NULL || addr == NULL) return 0;
	size_t ret = 0,templen = 0;
	while(*dns)
	{
		templen = (size_t)(*dns);	//length 
		dns++;						//point to next
		memcpy(addr,dns,templen);	//copy
		addr+=templen;dns+=templen;	//update pointers
		*addr='.'; addr++;			//add '.'
		ret += templen+1;			//update ret
	}
	*(--addr) = '\0';				//add '\0'
	return ret+1;					//last '\0' in dns
}


/**
 * IMPLEMENTATION OF fill_request(re_num,addrs,payload)
 */
size_t fill_request(int req_num,  char * req_addr[], int id, char * payload)
{
	int ret = 0;
	if(req_num < 0 || req_addr == NULL || payload == NULL) 
		return 0;
	//FILL HEADER
	struct DNS_header_t * pheader;
	memset(payload,0,sizeof(struct DNS_header_t));
	pheader = (struct DNS_header_t *) payload;
	pheader->ID = htons(id);
	pheader->tag = htons(REQUEST_TAG);
	pheader->QDcount = htons(req_num);
	
	ret += sizeof(struct DNS_header_t);
	payload += sizeof(struct DNS_header_t);


	//Fill body
	size_t tempsize = 0;
    struct DNS_request_t *requset_header;	
	for(int i=0;i<req_num;i++)
	{
		tempsize = addr2dns(req_addr[i], payload);	//FILL QNAME FIELD
		ret+=tempsize;
		payload += tempsize;

		requset_header = (struct DNS_request_t *) payload;
		requset_header->qtype = htons(1);
		requset_header->qclass = htons(1);
		ret += sizeof(struct DNS_request_t);
		payload += sizeof(struct DNS_request_t);
	}
	return ret;
}

size_t fill_response(int req_num,  char * req_addrs[], int id,  struct in_addr * res_addrs[], char * payload, int flag)
{
	if(req_num < 0 || req_addrs == NULL ||
		   (res_addrs == NULL && flag == RESPOND_TAG)
		   || payload == NULL)
		return 0;
	size_t ret = 0;
	// FILL HEADER
	memset(payload,0,sizeof(struct DNS_header_t));
	struct DNS_header_t * pheader = (struct DNS_header_t *)payload;
	pheader->ID = htons(id);
	pheader->tag = htons(flag);
	pheader->ANcount = htons(req_num);
	ret += sizeof(struct DNS_header_t);
	payload += sizeof(struct DNS_header_t);
	

	//FILL BODY
	if(flag == RESPOND_TAG)
	{
		size_t tempsize = 0;
		struct DNS_respond_t * res_header;
		for(int i=0;i<req_num;i++)
		{
			//fill name
			tempsize = addr2dns(req_addrs[i],payload);
			ret+=tempsize; payload+=tempsize;

			//fill DNS_respond_t
			res_header = (struct DNS_respond_t *)payload;
			res_header->rtype = htons(1);
			res_header->rclass = htons(1);
			res_header->ttl = 0;
			res_header->rd_length = htons(sizeof(struct in_addr));
			ret += sizeof(struct DNS_respond_t);
			payload += sizeof(struct DNS_respond_t);

			//fill RDATA
			//TODO: this line here is not good ...
			fprintf(stderr,"res_addr = %x\n",res_addrs[i]->s_addr);
			*(uint32_t*)payload = res_addrs[i]->s_addr;
			tempsize=sizeof(struct in_addr);
			ret+=tempsize; payload+=tempsize;
		}
	}
	return ret;
}

size_t resolve_request_info(char * payload, char *(*dst)[])
{
	if(dst==NULL || payload == NULL) return 0;

	struct DNS_header_t *pheader = (struct DNS_header_t *)payload;
	uint16_t cnt = ntohs(pheader->QDcount);
	if(cnt == 0) return 0;
	
	payload += sizeof(struct DNS_header_t);
	size_t templen=0;
	for(int i=0;i<cnt;i++)
	{	
		(*dst)[i] = (char*)malloc(BUFFER_MAX_SIZE);
		templen = dns2addr(payload, (*dst)[i]);
//		fprintf(stdout,"%s\n",(*dst)[i]);
		if(templen == 0)		//parse failed
			return 0;			//*dst[i] is null or payload is null
		payload += templen;
		payload += sizeof (struct DNS_request_t);
	}
	return cnt;

}


/**
 * HELPER FUNCTION open_clientfd
 *
 * Open a clinet sockfd, and bind it to "client_ip"
 *
 * @returns 0 if success, -1 otherwise
 */
int open_clientfd()
{
	int clientfd;
	if((clientfd = socket(AF_INET,SOCK_DGRAM,0)) == -1)
	{
		fprintf(stderr,"cannot open socket in resolve\n");
		return -1;	
	}

	if(bind(clientfd, 
				(struct sockaddr *)&client_ip, 
				sizeof client_ip
			) == -1)
	{
		fprintf(stderr,"cannot bind clinet address %s\n",
				inet_ntoa((client_ip.sin_addr)));
		close(clientfd);
		return -1;
	}

	return clientfd;
}




int set_client_ip( char * ip)
{
	if (inet_aton(ip, &(client_ip.sin_addr)) == 0)
	{
		fprintf(stderr,"set client ip failed\n");
		return -1;
	}
	client_ip.sin_family = AF_INET;
	client_ip.sin_port = dns_port;
	return 0;
}

int init_mydns( char *dnsIp, unsigned int dnsPort,  char *clientIp)
{
	dns_port = htons(dnsPort);
	set_client_ip(clientIp);
	if (inet_aton(dnsIp, &(dns_ip.sin_addr)) == 0) 
	{
		fprintf(stderr, "set dns ip failed\n");
		return -1;
	}
	dns_ip.sin_family = AF_INET;
	dns_ip.sin_port = dns_port;
	
	int dp = ntohs(dns_ip.sin_port);
	int cp = ntohs(client_ip.sin_port);
	fprintf(stderr,"dns_ip (%s:%d)\n",inet_ntoa(dns_ip.sin_addr),
			dp);
	fprintf(stderr,"cli_ip (%s:%d)\n",inet_ntoa(client_ip.sin_addr),
			cp);
	return 0;
}


struct addrinfo * newAddrinfo()
{
	struct addrinfo * ret;
	ret = (struct addrinfo *)malloc(sizeof(struct addrinfo));
	bzero(ret,sizeof(struct addrinfo));
	ret->ai_flags = AI_NUMERICSERV;
	ret->ai_family = AF_INET;
	ret->ai_socktype = SOCK_STREAM;
	ret->ai_protocol = 0;
	ret->ai_canonname = NULL;
	return ret;
}

size_t resolve_respond_info(char * payload, struct addrinfo ** info)
{
	*info = newAddrinfo();
	struct addrinfo * curr = *info;

	struct DNS_header_t * ph = (struct DNS_header_t *)payload;
	uint16_t cnt = ntohs(ph->ANcount);
	if(cnt == 0) return 0;

	payload += sizeof(struct DNS_header_t);

	size_t templen;
	char tempbuf[BUFFER_MAX_SIZE];
	struct sockaddr_in *psockaddr;
	for(int i=0;i<cnt;i++)
	{
		//get hostname
		templen = dns2addr(payload,tempbuf);
		payload+=templen;
		payload+=sizeof(struct DNS_respond_t);

	
		//get ip_addr
		psockaddr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr));
		psockaddr->sin_addr = *(struct in_addr *)payload;
		payload+=sizeof(struct in_addr);
				

		//fill addrinfo
		curr->ai_addr = (struct sockaddr*)psockaddr;
		curr->ai_addrlen = sizeof(struct sockaddr);
		if(curr != *info)
		{
			curr->ai_next = (*info)->ai_next;
			(*info)->ai_next = curr;
		}
	}
	return cnt;
}

int resolve( char *node,  char *service,
			 struct addrinfo *hints, struct addrinfo **res)
{
	transcation_id++;
	//INITIALIZE
	char send_buf[MAX_PACKET_SIZE], recv_buf[MAX_PACKET_SIZE];
	bzero(send_buf,sizeof(send_buf));
	bzero(recv_buf,sizeof(recv_buf));
	
	int client_fd = open_clientfd();

	//SEND_REQUEST
	size_t send_len = fill_request(1, &node, transcation_id,send_buf);
	sendto(client_fd, send_buf, send_len, 0,
			(struct sockaddr *)&dns_ip, sizeof dns_ip);

	//RECV RESPOND
	socklen_t addrlen;
	struct sockaddr peer;
	size_t packnum = 0;
	int num;
	struct addrinfo * curr, * listend;
	while(packnum < 1)
	{
		if((num = recvfrom(client_fd, recv_buf, MAX_PACKET_SIZE,
						0, &peer, &addrlen)) == -1)
		{
			int err = errno;
			fprintf(stderr,"recvfrom error in resolve() err = %d\n", err);
			return -1;
		}
		
		size_t temp = resolve_respond_info(recv_buf,&curr); 
		if(temp == 0) continue;		//parse failed...
		packnum+=temp;
		
		//GET THE END OF THE LIST JUST RETURNED
		listend = curr;
		while(listend) listend = listend->ai_next;

		//INSERT CURR INTO RES
		*res = curr;

	}
	close(client_fd);
	return 0;
}


int mydns_freeaddrinfo(struct addrinfo *p)
{
	struct addrinfo * t;
	while(p)
	{
		if(!p->ai_addr) free(p->ai_addr);
		if(!p->ai_canonname) free(p->ai_canonname);
		t=p;
		free(p);
		p=t->ai_next;
	}
}

