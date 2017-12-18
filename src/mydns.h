#ifndef _MY_DNS_HH_
#define _MY_DNS_HH_

#include <netdb.h>
#include <stdint.h>
#include <bits/endian.h>
#include <sys/types.h>
//#include

/**
 * Struct for DNS packet header
 */
#define REQUEST_TAG		0x0000
#define RESPOND_TAG		0x8400
#define RCODE3_TAG		0x8403
#define MAX_PACKET_SIZE	1500
struct DNS_header_t
{
	uint16_t ID;			//id field
	uint16_t tag;			//0x0000 in request
							//0x8000 in standard reply
							//0x8003 in ERR_NOTFOUND...
	uint16_t QDcount;		//request number
	uint16_t ANcount;		//answer number
	uint16_t NScount;		//set to 0
	uint16_t ARcount;		//set to 0
};


/**
 * Struct for DNS request
 */
struct DNS_request_t
{
	uint16_t qtype;			//set to 1
	uint16_t qclass;		//set to 1
};



/**
 * Struct for DNS respond
 */
struct DNS_respond_t
{
	uint16_t rtype;			//set to 1
	uint16_t rclass;		//set to 1
	uint16_t ttl;			//set to 0
	uint16_t rd_length;		//the length of RDATA
};


/**
 * FUNCTION fill_requests
 * fill a dns request packet using the specified arguments
 * endian conversion will be performed
 *
 * @param req_num	the number of expected addresses
 *					*** NOT IN NETWORK ENDIAN !!! ***
 *
 * @param req_addrs	expected addresses
 * @param id		id field in dns header
 *					*** NOT IN NETWORK ENDIAN !!! ***
 *
 * @param payload	the payload to fill, include header
 *
 * @returns	the size of the payload filled if successed
 *			else return 0 [ERROR OCCURED]
 */		
size_t fill_request(int req_num,  char * req_addrs[],int id, char * payload);



/**
 * FUNCTION fill_response
 * fill a dns response packet using the specified arguments
 * endian convertion will be performed
 *
 * @param req_num	the number of expected addresses
 *					*** NOT IN NETWORK ENDIAN !!! ***
 *
 * @param req_addrs	expected addresses
 * @param res_addrs resolved addresses, into 4 byte ip_addr
 *					*** NOT IN NETWORK ENDIAN !!! ***
 *
 * @param id		id field in dns header
 *					*** NOT IN NETWORK ENDIAN !!! ***
 *
 * @param payload	the payload to fill,include header
 * @param flag		tag filed in the header
 * 
 * @returns the size of the payload filled if successed
 *			else return 0 [ERROR OCCURED]
 */
size_t fill_response(int req_num,  char * req_addrs[],int id,  
					 struct in_addr * res_addrs[], char * payload, int flag);



/**
 * FUNCTION resolve_request_info
 * read a dns packet and resolve the info into an array of in_addr
 *
 * @param payload	the packet
 * @param dst		the destination buffer
 *
 * @returns the number of addresses requested in the packet
 *
 */
size_t resolve_request_info(char * payload, char *(*dst)[]);


/**
 * FUNCTION resolve_respond_info(payload, info)
 *
 * resolve the repond packet and return a list of addrinfo
 * (using malloc)
 *
 * @param payload	the packet, including DNS header
 * @param info		the reference to the head pointer of 
 *					addrinfo list 
 *
 * @returns 0 if failed,
 *			a POSITIVE number indicates the number of 
 *			responds processed
 */
size_t resolve_respond_info(char *payload, struct addrinfo ** info);

/**
 * Initialize your client DNS library with the IP address and port number of
 * your DNS server.
 *
 * @param  dns_ip  The IP address of the DNS server.
 * @param  dns_port  The port number of the DNS server.
 * @param  client_ip  The IP address of the client
 *
 * @return 0 on success, -1 otherwise
 */
int init_mydns( char *dns_ip, unsigned int dns_port,  char *client_ip);



/** 
 * FUNCTION set_client_ip(client_ip)
 * set the source ip of resolve ... 
 * it matters when dns server use lsa
 *
 * @param client_ip		a string indicates the ip address of client
 *
 * @returns 0 on success, -1 otherwise
 */
int set_client_ip( char * client_ip);


/**
 * Resolve a DNS name using your custom DNS server.
 *
 * Whenever your proxy needs to open a connection to a web server, it calls
 * resolve() as follows:
 *
 * struct addrinfo *result;
 * int rc = resolve("video.pku.edu.cn", "8080", null, &result);
 * if (rc != 0) {
 *     // handle error
 * }
 * // connect to address in result
 * mydns_freeaddrinfo(result);
 *
 *
 * @param  node  The hostname to resolve.
 * @param  service  The desired port number as a string.
 * @param  hints  Should be null. resolve() ignores this parameter.
 * @param  res  The result. resolve() should allocate a struct addrinfo, which
 * the caller is responsible for freeing.
 *
 * @return 0 on success, -1 otherwise
 */

int resolve( char *node,  char *service, 
             struct addrinfo *hints, struct addrinfo **res);

/**
 * Release the addrinfo structure.
 *
 * @param  p  the addrinfo structure to release
 *
 * @return 0 on success, -1 otherwise
 */
int mydns_freeaddrinfo(struct addrinfo *p);


#endif
