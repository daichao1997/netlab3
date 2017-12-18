proxy: proxy.c csapp.c csapp.h
	gcc proxy.c csapp.c -o ../proxy -lpthread -std=gnu99

all: nameserver

mydns: mydns.c mydns.h
	gcc mydns.c mydns.h

nameserver: dns_server.c mydns.h mydns.c parse_lsa.h parse_lsa.c
	gcc dns_server.c mydns.h mydns.c parse_lsa.h parse_lsa.c -o ../nameserver
