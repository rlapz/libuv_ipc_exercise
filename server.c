#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"
#include "ipc.h"


typedef struct {
	uv_handle_t *handle;
	uv_buf_t     buffer;
} Context;


static void         _allocator(uv_handle_t *u, size_t size, uv_buf_t *buffer);
static uv_pipe_t   *_prep_ipc(uv_loop_t *u, const char sock_file[]);
static uv_signal_t *_prep_signal(uv_loop_t *u);
static void         _on_accept(uv_stream_t *u, int status);
static void         _on_signal(uv_signal_t *u, int sig);
static void         _on_walk(uv_handle_t *u, void *arg);
static void         _on_close(uv_handle_t *u);
static void         _on_recv(uv_stream_t *u, ssize_t res, const uv_buf_t *buffer);
static void         _on_send(uv_write_t *u, int res);
static int          _resp_hello(uv_buf_t *buffer);
static int          _resp_status(uv_buf_t *buffer);
static int          _resp_error(uv_buf_t *buffer, int req, int err, const char message[]);
static int          _resp_shutdown(uv_buf_t *buffer);


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


int
server_run(Server *s)
{
	int ret = -1;
	uv_pipe_t *const ipc = _prep_ipc(s->loop, s->sock_file);
	if (ipc == NULL)
		return -1;

	uv_signal_t *const signl = _prep_signal(s->loop);
	if (signl == NULL)
		goto out0;

	ret = uv_run(s->loop, UV_RUN_DEFAULT);
	if (ret < 0) {
		fprintf(stderr, "server: server_run: uv_run: %s\n", uv_strerror(ret));
		goto out1;
	}

	ret = uv_loop_close(s->loop);
	if (ret < 0)
		fprintf(stderr, "server: server_run: uv_loop_close: %s\n", uv_strerror(ret));

	uv_library_shutdown();
	return ret;

out1:
	if (uv_is_active((uv_handle_t *)signl)) {
		uv_close((uv_handle_t *)signl, NULL);
		free(signl);
	}

out0:
	if (uv_is_active((uv_handle_t *)ipc)) {
		uv_close((uv_handle_t *)ipc, NULL);
		free(ipc);
	}

	return -1;
}


/*
 * private
 */
static void
_allocator(uv_handle_t *u, size_t size, uv_buf_t *buffer)
{
	void *const mem = malloc(size);
	if (mem == NULL) {
		perror("server: _allocator: malloc: uv_buf_t");
		buffer->base = NULL;
		buffer->len = 0;
		return;
	}

	buffer->base = mem;
	buffer->len = size;
	(void)u;
}


static uv_pipe_t *
_prep_ipc(uv_loop_t *u, const char sock_file[])
{
	uv_pipe_t *const ipc = malloc(sizeof(uv_pipe_t));
	if (ipc == NULL) {
		perror("server: _prep_ipc: malloc: uv_pipe_t");
		return NULL;
	}

	int ret = uv_pipe_init(u, ipc, 0);
	if (ret < 0) {
		fprintf(stderr, "server: _prep_ipc: uv_pipe_init: %s\n", uv_strerror(ret));
		goto err0;
	}

	ret = uv_pipe_bind(ipc, sock_file);
	if (ret < 0) {
		fprintf(stderr, "server: _prep_ipc: uv_pipe_bind: %s\n", uv_strerror(ret));
		goto err1;
	}

	ret = uv_listen((uv_stream_t *)ipc, 32, _on_accept);
	if (ret < 0) {
		fprintf(stderr, "server: _prep_ipc: uv_listen: %s\n", uv_strerror(ret));
		goto err1;
	}

	return ipc;

err1:
	uv_close((uv_handle_t *)ipc, NULL);
err0:
	free(ipc);
	return NULL;
}


static uv_signal_t *
_prep_signal(uv_loop_t *u)
{
	uv_signal_t *const signl = malloc(sizeof(uv_signal_t));
	if (signl == NULL) {
		perror("server: _prep_signal: malloc: uv_signal_t");
		return NULL;
	}

	const int ret = uv_signal_init(u, signl);
	if (ret < 0) {
		fprintf(stderr, "server: _prep_signal: uv_signal_init: %s\n", uv_strerror(ret));
		free(signl);
		return NULL;
	}

	uv_signal_start(signl, _on_signal, SIGINT);
	return signl;
}


static void
_on_accept(uv_stream_t *u, int status)
{
	if (status < 0) {
		fprintf(stderr, "server: on_accept: %s\n", uv_strerror(status));
		return;
	}

	uv_pipe_t *const client = malloc(sizeof(uv_pipe_t));
	if (client == NULL) {
		perror("server: on_accept: malloc");
		return;
	}

	const int ret = uv_pipe_init(u->loop, client, 0);
	if (ret < 0) {
		fprintf(stderr, "server: uv_pipe_init: %s\n", uv_strerror(ret));
		free(client);
		return;
	}

	/* https://docs.libuv.org/en/v1.x/stream.html#c.uv_accept
	 *  When the uv_connection_cb (this function) callback is called it is guaranteed
	 *  that this (below) function will complete successfully the first time. 
	 */
	uv_accept(u, (uv_stream_t *)client);

	/* test */
	printf("new client: %p\n", (void *)client);

	((uv_handle_t *)client)->data = "hello";

	uv_read_start((uv_stream_t *)client, _allocator, _on_recv);
}


static void
_on_signal(uv_signal_t *u, int sig)
{
	printf("\nsignal: %d\n", sig);
	const int ret = uv_loop_close(u->loop);
	if (ret == UV_EBUSY)
		uv_walk(u->loop, _on_walk, NULL);
	else
		fprintf(stderr, "server: on_signal: uv_loop_close: %s\n", uv_strerror(ret));
}


static void
_on_walk(uv_handle_t *u, void *arg)
{
	(void)arg;
	uv_close(u, _on_close);
}


static void
_on_close(uv_handle_t *u)
{
	printf("server: on_close: closed: %p\n", (void *)u);
	free(u);
}


static void
_on_recv(uv_stream_t *u, ssize_t res, const uv_buf_t *buffer)
{
	int ret = 0;
	int is_einval = 0;
	uv_write_t *writer;
	//uv_buf_t *_buffer;
	Context *context;


	char *const data = buffer->base;
	if (res < 0) {
		fprintf(stderr, "server: _on_recv: %s\n", uv_strerror(res));
		goto out0;
	}

	if (res == UV_EOF)
		goto out0;

	if (res == 0) {
		fprintf(stderr, "server: _on_recv: empty request\n");
		goto out0;
	}

	printf("%p: req: %.*s\n", (void *)u, (int)res - 1, data);

	IpcRequest req;
	ret = ipc_request_parse(&req, data, res - 1);
	switch (ret) {
	case IPC_PARSE_SUCCESS: break;
	case IPC_PARSE_EINVAL: is_einval = 1; break;
	default: goto out1;
	}

	writer = malloc(sizeof(uv_write_t));
	if (writer == NULL) {
		perror("server: _on_recv: malloc: uv_write_t");
		goto out0;
	}
	
	context = malloc(sizeof(Context));
	if (context == NULL) {
		perror("server: _on_recv: malloc: uv_buf_t");
		goto out1;
	}

	if (is_einval) {
		ret = _resp_error(&context->buffer, req.code, IPC_RES_ERR_BAD_REQUEST, "bad request");
	} else {
		switch (req.code) {
		case IPC_REQ_HELLO: ret = _resp_hello(&context->buffer); break;
		case IPC_REQ_STATUS: ret = _resp_status(&context->buffer); break;
		case IPC_REQ_SHUTDOWN: ret = _resp_shutdown(&context->buffer); break;
		default: ret = -1; break;
		}
	}

	if (ret < 0)
		goto out2;

	context->handle = (uv_handle_t *)u;
	writer->data = context;

	ret = uv_write(writer, u, &context->buffer, 1, _on_send);
	if (ret < 0) {
		fprintf(stderr, "server: _on_recv: uv_write: %s\n", uv_strerror(ret));
		goto out3;
	}

	free(data);
	return;

out3:
	free(context->buffer.base);
out2:
	free(context);
out1:
	free(writer);
out0:
	free(data);
	uv_close((uv_handle_t *)u, _on_close);
}


static void
_on_send(uv_write_t *u, int res)
{
	Context *const context = (Context *)u->data;
	if (res < 0)
		fprintf(stderr, "server: _on_send: %p: %s\n", (void *)u->handle, uv_strerror(res));

	printf("_on_send: %p: %d\n", u, res);

	uv_close(context->handle, _on_close);
	free(context->buffer.base);
	free(context);
	free(u);
}


static int
_resp_hello(uv_buf_t *buffer)
{
	char *const resp = ipc_response_build_hello();
	if (resp == NULL) {
		perror("server: _resp_hello: ipc_response_build_hello");
		return -1;
	}

	buffer->base = resp;
	buffer->len = strlen(resp);
	return 0;
}


static int
_resp_status(uv_buf_t *buffer)
{
	/*
	 * TODO
	 */
	buffer->base = NULL;
	buffer->len = 0;
	return 0;
}


static int
_resp_error(uv_buf_t *buffer, int req, int err, const char message[])
{
	char *const resp = ipc_response_build_error(req, err, message);
	if (resp == NULL) {
		perror("server: _resp_hello: ipc_response_build_error");
		return -1;
	}

	buffer->base = resp;
	buffer->len = strlen(resp);
	return 0;
}


static int
_resp_shutdown(uv_buf_t *buffer)
{
	char *const resp = ipc_response_build_shutdown();
	if (resp == NULL) {
		perror("server: _resp_hello: ipc_response_build_shutdown");
		return -1;
	}

	buffer->base = resp;
	buffer->len = strlen(resp);
	return 0;
}

