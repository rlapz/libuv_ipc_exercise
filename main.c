#include <stdio.h>
#include <string.h>

#include "server.h"
#include "client.h"


#define SERVER_SOCKET_FILE "/tmp/kvrt.sock"


static int  _run_client(const char cmd[]);
static int  _run_server(void);


/*
 * function impls
 */
static int
_run_client(const char cmd[])
{
	return -client_run(SERVER_SOCKET_FILE, cmd);
}


static int
_run_server(void)
{
	Server server;
	if (server_init(&server, SERVER_SOCKET_FILE) < 0)
		return 1;

	return -server_run(&server);
}


/*
 * entry point
 */
int
main(int argc, char *argv[])
{
	if (argc < 2)
		return 1;

	if (strcmp(argv[1], "client") == 0) {
		if (argc == 3)
			return _run_client(argv[2]);
	} else if (strcmp(argv[1], "server") == 0) {
		if (argc == 2)
			return _run_server();
	}

	return 1;
}

