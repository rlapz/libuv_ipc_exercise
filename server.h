#ifndef __SERVER_H__
#define __SERVER_H__


#include <stdint.h>
#include <uv.h>


typedef struct {
	int __dummy;
} SClient;

typedef struct {
	const char *sock_file;
	uv_loop_t  *loop;
} Server;

int server_init(Server *s, const char sock_file[]);
int server_run(Server *s);


#endif

