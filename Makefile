all: proxy nameserver

proxy: src/proxy.c src/csapp.c src/csapp.h
	gcc src/proxy.c src/csapp.c -o proxy -lpthread -std=gnu99

mydns: src/mydns.c src/mydns.h
	gcc src/mydns.c src/mydns.h

nameserver: src/dns_server.c src/mydns.h src/mydns.c src/parse_lsa.h src/parse_lsa.c
	gcc src/dns_server.c src/mydns.h src/mydns.c src/parse_lsa.h src/parse_lsa.c -o nameserver
