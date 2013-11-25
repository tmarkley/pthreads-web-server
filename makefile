project:	server.c client.c
	gcc -o server -pthread server.c
	gcc -o client client.c
