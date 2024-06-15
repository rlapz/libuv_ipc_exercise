#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "client.h"
#include "ipc.h"


static int  _parse_cmd(const char cmd[]);
static int  _open_sock_file(const char sock_file[]);
static int  _send_request(int req_code, int fd);
static int  _recv_response(IpcResponse *resp, int fd);
static void _print_response(const IpcResponse *resp, int req_code);


/*
 * public
 */
int
client_run(const char sock_file[], const char cmd[])
{
	signal(SIGPIPE, SIG_IGN);

	const int cmd_num = _parse_cmd(cmd);
	if (cmd_num == 0)
		return -1;

	const int fd = _open_sock_file(sock_file);
	if (fd < 0)
		return -1;

	int ret = -1;
	if (_send_request(cmd_num, fd) < 0)
		goto out0;

	IpcResponse resp;
	if (_recv_response(&resp, fd) < 0)
		goto out0;

	_print_response(&resp, cmd_num);
	ret = 0;

out0:
	close(fd);
	return ret;
}


/*
 * private
 */
static int
_parse_cmd(const char cmd[])
{
	if (strcasecmp(cmd, "hello") == 0)
		return IPC_REQ_HELLO;

	if (strcasecmp(cmd, "status") == 0)
		return IPC_REQ_STATUS;

	if (strcasecmp(cmd, "shutdown") == 0)
		return IPC_REQ_SHUTDOWN;

	fprintf(stderr, "client: _parse_cmd: invalid command\n");
	return 0;
}


static int
_open_sock_file(const char sock_file[])
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };

	const size_t sock_file_len = strlen(sock_file);
	if (sock_file_len >= sizeof(addr.sun_path)) {
		fprintf(stderr, "client: _open_sock_file: invalid 'sock_file' length\n");
		return -1;
	}

	const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("client: _open_sock_file: socket");
		return -1;
	}

	memcpy(addr.sun_path, sock_file, sock_file_len + 1);

	const size_t len = sock_file_len + sizeof(addr.sun_family);
	if (connect(fd, (const struct sockaddr *)&addr, len) < 0) {
		fprintf(stderr, "client: _open_sock_file: connect: %s: %s\n", sock_file, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}


static int
_send_request(int req_code, int fd)
{
	char *req = NULL;
	switch (req_code) {
	case IPC_REQ_HELLO: req = ipc_request_build_hello(); break;
	case IPC_REQ_STATUS: req = ipc_request_build_status(); break;
	case IPC_REQ_SHUTDOWN: req = ipc_request_build_shutdown(); break;
	}

	if (req == NULL) {
		fprintf(stderr, "client: _send_request: failed to build request\n");
		return -1;
	}

	int ret = -1;
	const size_t len = strlen(req) + 1;
	for (size_t sent = 0; sent < len;) {
		const ssize_t sn = send(fd, req + sent, len - sent, 0);
		if (sn <= 0) {
			if (sn < 0) {
				perror("client: _send_request: send");
				goto out0;
			}

			break;
		}
		
		sent += (size_t)sn;
	}

	ret = 0;

out0:
	free(req);
	return ret;
}


static int
_recv_response(IpcResponse *resp, int fd)
{
	char buffer[8192];
	const size_t buffer_size = sizeof(buffer);


	size_t recvd = 0;
	while (recvd < buffer_size) {
		const ssize_t rv = recv(fd, buffer + recvd, buffer_size - recvd, 0);
		if (rv < 0) {
			perror("client: _recv_response: recv");
			return -1;
		}

		if (rv == 0)
			break;

		recvd += (size_t)rv;
	}

	if (recvd == 0) {
		fprintf(stderr, "client: _recv_response: recv: 0 byte\n");
		return -1;
	}

	buffer[recvd] = '\0';

	const int ret = ipc_response_parse(resp, buffer, recvd);
	switch (ret) {
	case IPC_PARSE_SUCCESS:
		return 0;
	case IPC_PARSE_ENOMEM:
		fprintf(stderr, "client: _recv_response: ipc_response_parse: failed to allocate memory\n");
		break;
	case IPC_PARSE_EPART:
	case IPC_PARSE_EINVAL:
		fprintf(stderr, "client: _recv_response: ipc_response_parse: invalid response\n");
		break;
	}

	return -1;
}


static void
_print_response(const IpcResponse *resp, int req_code)
{
	const int rcode = resp->request_code;
	if (rcode != req_code) {
		printf("response: invalid response: request does not match!\n");
		return;
	}

	const int code = resp->code;
	if (code != IPC_RES_OK) {
		printf("response: %s\n", ipc_response_code_str(code));
		return;
	}

	const IpcBodyStatus *const status = &resp->status;

	switch (rcode) {
	case IPC_REQ_HELLO:
	case IPC_REQ_SHUTDOWN:
		printf("response: %s\n", resp->message);
		break;
	case IPC_REQ_STATUS:
		printf("response: \n"
		       " cpu cores:       %u\n"
		       " memory usage:    %zu\n"
		       " memory capacity: %zu\n",
		       status->cpu_cores, status->memory_usage, status->memory_capacity);
		break;
	default:
		printf("hmm...\n");
		break;
	}
}

