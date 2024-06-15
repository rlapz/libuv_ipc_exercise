#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "server.h"


static int  _prep_signal(uv_loop_t *u);
static int  _prep_ipc(uv_loop_t *u, const char sock_path[]);
static void _on_signal(uv_signal_t *u, int sig);
static void _on_walk(uv_handle_t *u, void *arg);
static void _on_accept(uv_stream_t *u, int status);
static void _on_close(uv_handle_t *u);


/*
 * public
 */
int
server_init(Server *s, const char sock_file[])
{
	uv_loop_t *const loop = uv_default_loop();
	if (loop == NULL) {
		fprintf(stderr, "server: server_init: uv_loop_default: failed to initialize\n");
		return -1;
	}

	s->sock_file = sock_file;
	s->loop = loop;
	return 0;
}


void
server_deinit(Server *s)
{
	const int ret = uv_loop_close(s->loop);
	if (ret < 0)
		fprintf(stderr, "server: server_deinit: uv_loop_close: %s\n", uv_strerror(ret));
}


int
server_run(Server *s)
{
	uv_pipe_t *const sock = malloc(sizeof(uv_pipe_t));
	if (sock == NULL) {
		perror("server: server_run: malloc: uv_pipe_t");
		return -1;
	}

	uv_signal_t *const signl = malloc(sizeof(uv_signal_t));
	if (signl == NULL) {
		perror("server: server_run: malloc: uv_signal_t");
		free(sock);
		return -1;
	}

	int ret = uv_signal_init(s->loop, signl);
	if (ret < 0) {
		fprintf(stderr, "server: server_run: uv_signal_init: %s\n", uv_strerror(ret));
		return -1;
	}

	uv_signal_start(signl, on_signal, SIGINT);


	ret = uv_pipe_init(s->loop, sock, 0);
	if (ret < 0) {
		fprintf(stderr, "server: server_run: uv_pipe_init: %s\n", uv_strerror(ret));
		return -1;
	}

	unlink(s->sock_file);

	ret = uv_pipe_bind(sock, s->sock_file);
	if (ret < 0) {
		fprintf(stderr, "server: server_run: uv_pipe_bind: %s: %s\n", s->sock_file, uv_strerror(ret));
		goto out0;
	}

	ret = uv_listen((uv_stream_t *)sock, 32, on_accept);
	if (ret < 0) {
		fprintf(stderr, "server: server_run: uv_pipe_listen: %s: %s\n", s->sock_file, uv_strerror(ret));
		goto out0;
	}

	ret = uv_run(s->loop, UV_RUN_DEFAULT);
	if (ret < 0) {
		fprintf(stderr, "server: server_run: uv_run: %s\n", uv_strerror(ret));
		goto out0;
	}

	ret = uv_loop_close(s->loop);
	if (ret < 0)
		fprintf(stderr, "server: server_run: uv_loop_close: %s\n", uv_strerror(ret));

out0:
	if (uv_is_active((uv_handle_t *)sock)) {
		uv_close((uv_handle_t *)sock, NULL);
		free(sock);
	}

	unlink(s->sock_file);
	return ret;
}


/*
 * private
 */
static int
_prep_signal(uv_loop_t *u)
{
}


static int
_prep_ipc(uv_loop_t *u, const char sock_path[])
{
}


static void
on_signal(uv_signal_t *u, int sig)
{
	printf("\nsignal: %d\n", sig);
	const int ret = uv_loop_close(u->loop);
	if (ret == UV_EBUSY)
		uv_walk(u->loop, on_walk, NULL);
	else
		fprintf(stderr, "server: on_signal: uv_loop_close: %s\n", uv_strerror(ret));
}


static void
on_walk(uv_handle_t *u, void *arg)
{
	uv_close(u, on_close);
}


static void
on_accept(uv_stream_t *u, int status)
{
	if (status < 0) {
		fprintf(stderr, "server: on_accept: %s\n", uv_strerror(status));
		return;
	}

	int ret;
	uv_pipe_t *const client = malloc(sizeof(uv_pipe_t));
	if (client == NULL) {
		perror("server: on_accept: malloc");
		return;
	}

	ret = uv_pipe_init(u->loop, client, 0);
	if (ret < 0) {
		fprintf(stderr, "server: uv_pipe_init: %s\n", uv_strerror(ret));
		goto out0;
	}

	ret = uv_accept(u, (uv_stream_t *)client);
	if (ret < 0) {
		fprintf(stderr, "server: uv_accept: %s\n", uv_strerror(ret));
		goto out0;
	}

	/* test */
	printf("new client: %p\n", (void *)client);

	/* TODO: init recv */
	return;

out0:
	uv_close((uv_handle_t *)client, NULL);
	free(client);
}


static void
on_close(uv_handle_t *u)
{
	printf("server: on_close: closed: %p\n", (void *)u);
	free(u);
}

