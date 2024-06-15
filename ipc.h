#ifndef __IPC_H__
#define __IPC_H__


#include <stddef.h>


/* request format:
 *
 * {
 * 	"code": REQ_TYPE
 * }
 */

/* response format:
 *
 * {
 * 	"code": RES_TYPE,
 * 	"request_code": REQ_TYPE,
 * 	"body": {
 * 		...
 * 	}
 *
 * body: Status
 * {
 * 	"cpu_cores": NUM,
 * 	"mem_usage": NUM,
 * 	"mem_capacity": NUM
 * }
 */


#define IPC_MESSAGE_SIZE (256)


enum {
	IPC_REQ_HELLO = 1,
	IPC_REQ_STATUS,
	IPC_REQ_SHUTDOWN,

	/* -------------------------------- */

	IPC_RES_OK = 10,
	IPC_RES_ERR_BAD_REQUEST,
	IPC_RES_ERR_BAD_RESPONSE,
	IPC_RES_ERR_INTERNAL,
	IPC_RES_ERR_UNKNOWN,
};

enum {
	IPC_PARSE_SUCCESS = 0,
	IPC_PARSE_ENOMEM,
	IPC_PARSE_EPART,
	IPC_PARSE_EINVAL,
};


/*
 * Helpers
 */
const char *ipc_request_code_str(int code);
const char *ipc_response_code_str(int code);


/*
 * Request
 */
typedef struct {
	int code;
} IpcRequest;

char *ipc_request_build_hello(void);
char *ipc_request_build_status(void);
char *ipc_request_build_shutdown(void);
int   ipc_request_parse(IpcRequest *r, const char json[], size_t len);


/*
 * Response
 */
typedef struct {
	unsigned cpu_cores;
	size_t   memory_usage;
	size_t   memory_capacity;
} IpcBodyStatus;

typedef struct {
	int code;
	int request_code;
	union {
		IpcBodyStatus status;
		char          message[IPC_MESSAGE_SIZE];
	};
} IpcResponse;

char *ipc_response_build_hello(void);
char *ipc_response_build_status(const IpcBodyStatus *status);
char *ipc_response_build_shutdown(void);
char *ipc_response_build_error(int req, int res, const char message[]);
int   ipc_response_parse(IpcResponse *r, const char json[], size_t len);


#endif

