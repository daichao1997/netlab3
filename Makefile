proxy: src/proxy.c src/csapp.c src/csapp.h
	gcc src/proxy.c src/csapp.c -o proxy -lpthread -std=gnu99
