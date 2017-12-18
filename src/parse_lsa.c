#include "parse_lsa.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_NODE_NUMBER 1000
#define MAX_NAME_LEN	25

#define bool int
#define true 1
#define false 0
static const int MAX_LINE_SIZE = 200;
static char * names[MAX_NODE_NUMBER];
static struct node_t nodes[MAX_NODE_NUMBER];
static struct lsa_record lsas[MAX_NODE_NUMBER];
static size_t count=0;

/**
 * HELP FUNCTION DEFINITIONS HERE
 */

int update_node(int position, const char * neighs);
struct edge_t * newEdge(int id,int w);
void free_edge_list(struct edge_t * header);
void print_node(struct node_t * node);		//for debug


/**
 * =======================================
 *				IMPLEMENTATIONS
 * =======================================
 */

/**
 * HELPER FUNCTION newEdge(id,w)
 *
 * the "constructor" of struct edge_t
 * specify id and weight field with id and w
 * field "next" if filled with NULL
 *
 * @param id	dstid field
 * @param w		weight field
 *
 * @returns the new object
 */
struct edge_t *newEdge(int id,int w)
{
	struct edge_t * ret = (struct edge_t *) malloc(sizeof(struct edge_t *));
	ret->weight = w;
	ret->dstid = id;
	ret->next = NULL;
	return ret;
}



int find_name(const char * name, struct node_t ** pnoderef)
{
	for(size_t i=0;i<count;i++)
	{
		if(strcmp(name,names[i]) == 0)
		{
			if(pnoderef != NULL) *pnoderef = &nodes[i];
			return i;
		}
	}
	return -1;
}


int add_name(const char * name)
{
	int ret = find_name(name,NULL);
	if(ret != -1) return ret;	//already exist, add failed

	/*add name to names, add node to nodes*/
	names[count] = (char*)malloc(MAX_NAME_LEN);
	strcpy(names[count],name);	//add name to names
	nodes[count].id = count;	//initialize nodes[count]
	nodes[count].neigh = NULL;
	lsas[count].id = count;		//initialize lsa records
	lsas[count].maxseq = -1;
	count++;					//increase count
	return -1;
}


/**
 * HELPER FUNTION update_node(position, neighs)
 *
 * Update the node "nodes[position]", using neighs
 * And frees the previous edge list
 *
 * @param position	the position of the node being updated
 * @param neighs	a  string  specifies  neighbourhoods , 
 *					splited by ','
 *
 * @returns 0 if successful
 *			-1 else
 */
int update_node(int position,const char * neighs)
{
	char buf[MAX_LINE_SIZE],name[MAX_NAME_LEN];
	bzero(buf,sizeof buf);
	bzero(name,sizeof name);
	strcpy(buf,neighs);

	struct edge_t *header = newEdge(-1,-1),		  //header
				  *p;
	char *token = strtok(buf,",");		//initialize token
	while(token)
	{
		strcpy(name,token);
		int ret = add_name(name);		//if new, add it into list
		
		/*insert edge into new list*/
		p = newEdge(ret, 1);			//id is ret, distance is 1
		p->next = header->next;
		header->next = p;

		token = strtok(NULL,",");		//renew token
	}
	
	free_edge_list(nodes[position].neigh);	//free old list
	nodes[position].neigh = header->next;	//update

	return 0;
}



int init_lsa(FILE * f)
{
	if(!f) return -1;

	char linebuf[MAX_LINE_SIZE],neighs[MAX_LINE_SIZE];
	char sender[MAX_NAME_LEN]; int seqnum;
	while(fgets(linebuf,MAX_LINE_SIZE,f))
	{
		bzero(sender,sizeof(sender));
		sscanf(linebuf,"%s %d %s",sender,&seqnum,neighs);
		int position = add_name(sender);
		if(lsas[position].maxseq <= seqnum)			//NEED UPDATE!
		{
			update_node(position, neighs);
		}
	}

//	for(size_t i=0;i<count;i++) print_node(&nodes[i]);
	return 0;
}


void init_source(int src, int cnt, int *dis)
{
	dis[src]=0;
	for(int i=0;i<cnt;i++)
		if(i!=src) dis[i] = LSA_INFINITY_DIS;
}
void relax(int dst,int w,int * dis)
{
	if(dis[dst]>w) dis[dst]=w;
}
int find_min(int cnt,bool * q,int * dis)
{
	int min = LSA_INFINITY_DIS,minpos=0;
	for(int i=0;i<cnt;i++)
	{
		if(!q[i]) continue;
		if(dis[i]<min)
		{
			min=dis[i];
			minpos=i;
		}
	}
	return minpos;
}
int dijkstra(struct node_t *pstart, struct node_t * pend)
{
	if(pstart == pend) return 0;

	/*Initialize distance*/
	int dis[MAX_NODE_NUMBER];
	init_source(pstart->id,count,dis);
	
	/*Initialize queue*/
	bool q[MAX_NODE_NUMBER];int qsize;
	bzero(q,sizeof(q));
	for(size_t i=0;i<count;i++) q[i]=true;
	qsize = count;

	struct edge_t * curr;
	while(qsize > 0)
	{
	//	fprintf(stderr,"qsize = %d\n ",qsize);
		int u = find_min(count,q,dis);	//find min dis in Queue
		q[u] = false; qsize --;			//remove u from Q

		curr = nodes[u].neigh;			//enumrate edges
		while(curr)
		{
			int v=curr->dstid;
			relax(v, dis[u]+curr->weight, dis);
			curr = curr->next;
		}

	}
	//print_node(pstart);
	//print_node(pend);
	//fprintf(stderr,"%d\n",dis[pend->id]);
	return dis[pend->id];
}


/**
 * HELPER FUNCTION free_edge_list(header)
 * free a edge list starts at header
 */
void free_edge_list(struct edge_t * header)
{
	struct edge_t *p;
	while(header)
	{
		p=header;
		header = header->next;
		free(p);
	}
}
void free_lsa_resources()
{
	for(size_t i=0;i<count;i++)
	{
		free(nodes[i].neigh);
		free(names[i]);
	}
}



//FOR DEBUG
void print_node(struct node_t * node)
{
	fprintf(stderr,"{node %d, name = %s, edges are [ ",node->id,names[node->id]);
	struct edge_t * p = node->neigh;
	while(p)
	{
		fprintf(stderr,"(%d,%d) ",p->dstid,p->weight);
		p=p->next;
	}
	fprintf(stderr,"] }\n");
}
