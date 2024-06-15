#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipc.h"
#include "json.h"



static char *_str_builder(const char fmt[], ...);
static int   _parse_json(json_value_t **json_obj, const char json[], size_t len);
static void  _parse_message(char message[], const json_object_t *body);
static int   _parse_status(IpcBodyStatus *s, const json_object_t *body);


/*
 * public
 */
/*
 * Helpers
 */
const char *
ipc_request_code_str(int code)
{
	switch (code) {
	case IPC_REQ_HELLO: return "hello";
	case IPC_REQ_STATUS: return "status";
	case IPC_REQ_SHUTDOWN: return "shutdown";
	}

	return "unknown";
}


const char *
ipc_response_code_str(int code)
{
	switch (code) {
	case IPC_RES_OK: return "ok";
	case IPC_RES_ERR_BAD_REQUEST: return "bad request";
	case IPC_RES_ERR_BAD_RESPONSE: return "bad response";
	case IPC_RES_ERR_INTERNAL: return "internal server";
	}

	return "unknown";
}


/*
 * Request
 */
char *
ipc_request_build_hello(void)
{
	return _str_builder("{\"code\":%d}", IPC_REQ_HELLO);
}


char *
ipc_request_build_status(void)
{
	return _str_builder("{\"code\":%d}", IPC_REQ_STATUS);
}


char *
ipc_request_build_shutdown(void)
{
	return _str_builder("{\"code\":%d}", IPC_REQ_SHUTDOWN);
}


int
ipc_request_parse(IpcRequest *r, const char json[], size_t len)
{
	json_value_t *jsp;

	int ret = _parse_json(&jsp, json, len);
	if (ret != IPC_PARSE_SUCCESS)
		return ret;

	ret = IPC_PARSE_EINVAL;

	const json_object_t *const root_obj = json_value_as_object(jsp);
	if (root_obj == NULL)
		goto out0;

	if (root_obj->length != 1)
		goto out0;

	const json_object_element_t *const ename = root_obj->start; 
	if (strcmp(ename->name->string, "code") != 0)
		goto out0;

	const json_number_t *const code = json_value_as_number(ename->value);
	if (code == NULL)
		goto out0;

	r->code = atoi(code->number);
	ret = IPC_PARSE_SUCCESS;

out0:
	free(jsp);
	return ret;
}


/*
 * Response
 */
char *
ipc_response_build_hello(void)
{
	return _str_builder("{\"code\":%d, \"request_code\":%d, \"body\": {\"message\": \"%s\"}}",
			    IPC_RES_OK, IPC_REQ_HELLO, "well, hello friend!");
}


char *
ipc_response_build_status(const IpcBodyStatus *status)
{
	return _str_builder("{\"code\": %d, \"request_code\": %d, \"body\": {"
			    "\"cpu_cores\": %u, \"memory_usage\": %zu, \"memory_capacity\": %zu}}",
			    IPC_RES_OK, IPC_REQ_STATUS, status->cpu_cores, status->memory_usage,
			    status->memory_capacity);
}


char *
ipc_response_build_shutdown(void)
{
	return _str_builder("{\"code\":%d, \"request_code\":%d, \"body\": {\"message\":\"%s\"}}", IPC_RES_OK,
			    IPC_REQ_SHUTDOWN, "shutting down...");
}


char *
ipc_response_build_error(int req, int res, const char message[])
{
	size_t msg_len = strlen(message);
	if (msg_len >= IPC_MESSAGE_SIZE)
		msg_len = IPC_MESSAGE_SIZE - 1;

	return _str_builder("{\"code\":%d, \"request_code\":%d, \"body\": {\"message\":\"%s: %.*s\"}}", res, req,
			    ipc_response_code_str(res), (int)msg_len, message);
}


int
ipc_response_parse(IpcResponse *r, const char json[], size_t len)
{
	json_value_t *jsp;

	int ret = _parse_json(&jsp, json, len);
	if (ret != IPC_PARSE_SUCCESS)
		return ret;

	ret = IPC_PARSE_EINVAL;

	const json_object_t *const root_obj = json_value_as_object(jsp);
	if (root_obj == NULL)
		goto out0;

	// "body" is optional
	if ((root_obj->length < 2) || (root_obj->length > 3))
		goto out0;

	int i = 3;
	int code = 0;
	int request_code = 0;
	const json_object_t *body = NULL;
	for (const json_object_element_t *e = root_obj->start; (e != NULL) && (i > 0); e = e->next) {
		const char *const name = e->name->string;
		if (strcmp(name, "code") == 0) {
			const json_number_t *const _code = json_value_as_number(e->value);
			if (_code == NULL)
				break;

			code = atoi(_code->number);
			i--;
		} else if (strcmp(name, "request_code") == 0) {
			const json_number_t *const _rcode = json_value_as_number(e->value);
			if (_rcode == NULL)
				break;

			request_code = atoi(_rcode->number);
			i--;
		} else if (strcmp(name, "body") == 0) {
			body = json_value_as_object(e->value);
			i--;
		}
	}

	if (code != IPC_RES_OK) {
		_parse_message(r->message, body);
	} else {
		switch (request_code) {
		case IPC_REQ_HELLO:
		case IPC_REQ_SHUTDOWN:
			_parse_message(r->message, body);
			break;
		case IPC_REQ_STATUS:
			if (_parse_status(&r->status, body) != IPC_PARSE_SUCCESS)
				goto out1;
			break;
		default:
			r->message[0] = '\0';
			break;
		}
	}

	ret = IPC_PARSE_SUCCESS;

out1:
	r->code = code;
	r->request_code = request_code;

out0:
	free(jsp);
	return ret;
}


/*
 * private
 */
static char *
_str_builder(const char fmt[], ...)
{
	int ret;
	va_list va;


	/* determine required size */
	va_start(va, fmt);
	ret = vsnprintf(NULL, 0, fmt, va);
	va_end(va);

	if (ret < 0)
		return NULL;


	const size_t str_len = ((size_t)ret) + 1;
	char *const str = malloc(sizeof(char) * str_len);
	if (str == NULL)
		return NULL;

	va_start(va, fmt);
	ret = vsnprintf(str, str_len, fmt, va);
	va_end(va);

	if (ret < 0) {
		free(str);
		return NULL;
	}

	str[ret] = '\0';
	return str;
}


static int
_parse_json(json_value_t **json_obj, const char json[], size_t len)
{
	int ret = IPC_PARSE_SUCCESS;
	json_parse_result_t res;
	json_value_t *const jsp = json_parse_ex(json, len, json_parse_flags_default, NULL, NULL, &res);
	if (jsp == NULL) {
		assert(res.error != json_parse_error_none);
		switch (res.error) {
		case json_parse_error_expected_comma_or_closing_bracket: ret = IPC_PARSE_EPART; break;
		case json_parse_error_allocator_failed: ret = IPC_PARSE_ENOMEM; break;
		default: ret = IPC_PARSE_EINVAL; break;
		}
	}

	*json_obj = jsp;
	return ret;
}


static void
_parse_message(char message[], const json_object_t *body)
{
	size_t msg_len = 0;
	if ((body != NULL) && (body->length == 1)) {
		const json_object_element_t *const body_o = body->start;
		if (strcmp(body_o->name->string, "message") == 0) {
			const json_string_t *const msg_o = json_value_as_string(body_o->value);
			if (msg_o != NULL) {
				msg_len = msg_o->string_size;
				if (msg_len >= IPC_MESSAGE_SIZE)
					msg_len = IPC_MESSAGE_SIZE - 1;

				memcpy(message, msg_o->string, msg_len);
			}
		}
	}

	message[msg_len] = '\0';
}


static int
_parse_status(IpcBodyStatus *s, const json_object_t *body)
{
	if (body->length != 3)
		return IPC_PARSE_EINVAL;

	for (const json_object_element_t *e = body->start; (e != NULL); e = e->next) {
		const json_number_t *num;
		const char *const name = e->name->string;
		if (strcmp(name, "cpu_cores") == 0) {
			num = json_value_as_number(e->value);
			if (num == NULL)
				return IPC_PARSE_EINVAL;

			s->cpu_cores = (unsigned)atoi(num->number);
		} else if (strcmp(name, "memory_usage") == 0) {
			num = json_value_as_number(e->value);
			if (num == NULL)
				return IPC_PARSE_EINVAL;

			s->memory_usage = (size_t)atoi(num->number);
		} else if (strcmp(name, "memory_capacity") == 0) {
			num = json_value_as_number(e->value);
			if (num == NULL)
				return IPC_PARSE_EINVAL;

			s->memory_capacity = (size_t)atoi(num->number);
		}
	}

	return IPC_PARSE_SUCCESS;
}

