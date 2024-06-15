#!/bin/sh


cc -g -Wall -Wextra main.c ipc.c server.c client.c -luv -fsanitize=undefined -fsanitize=address \
	-o uvipc

#cc -g -Wall -Wextra main.c ipc.c server.c client.c -luv     -o uvipc

#cc -Wall -Wextra main.c ipc.c server.c client.c -luv     -o uvipc -O3

