#ifndef _PARSE_LSA_HH_
#define _PARSE_LSA_HH_

#include <stdio.h>
#include <limits.h>
#define LSA_INFINITY_DIS	INT_MAX
/**
 * STRUCT node_t
 * store the node in the graph
 * use adjoint node list..
 */
struct node_t;

/**
 * STRUCT edge_t
 * the adjoint node list used in node_t;
 */
struct edge_t;

/**
 * STRUCT lsa_record 
 * store the sender number and max seq number
 */
struct lsa_record;

/**
 *===================
 * STRUCT DEFINITION
 *===================
 */

struct node_t
{
	int id;
	struct edge_t * neigh;
};

struct edge_t
{
	int dstid;
	int weight;
	struct edge_t * next;
};

struct lsa_record
{
	int id;
	int maxseq;
};


/**
 * ======================
 *  FUNCTION DEFINITIONS
 * ======================
 */


/**
 * FUNCTION find_name(name,pnoderef)
 * give a specified name(addr such as 3.0.0.1, "router1"..)
 * find the node and fill the pnoderef
 * If no node find, *pnoderef will be NULL
 *
 * @param name		the specified name
 * @param pnoderef	the reference to the pointer which will be filled
 *					it'll be ignored if it's NULL
 *
 * @returns -1 if not found
 *			a NOT negative number if found
 */
int find_name(const char * name, struct node_t ** pnoderef);


/**
 * FUNCTION add_name(name)
 * Call find_name first, if find then add failed...
 * If not found, insert the name in namelist and add a new node
 * into node list
 *
 * @param name		the specified name
 *
 * @returns a positive number denotes the position of name
 */
int add_name(const char * name);



/** 
 * FUNCTION init_lsa(f)
 * Initialize all lists using a lsa file
 *
 * @param f		the lsa file
 *
 * @returns	0 if successful
 *			-1 else
 */
int init_lsa(FILE *f);	



/**
 * FUNCTION diskstra(pstart,pend)
 * Find the shortest path from pstart to pend, and return it
 *
 * @param pstart	the start node
 * @param pend		then end node
 *
 * @returns	LSA_INFINITY_DIS (e.g. INT_MAX) if no way
 *			0 if pstart == pend
 *			else a positive number indicate the shortest
 *			path length
 */
int dijkstra(struct node_t *pstart,
				struct node_t *pend);


/**
 * FUNCTION free_lsa_resources()
 *
 * Cleaning up
 */
void free_lsa_resources();

#endif
