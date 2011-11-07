/*
 * 
 *
 * Copyright (C) 2005, wucan <can_wu@cnbvcom.com>
 *
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "message.h"


int msg_verbose_str_to_type (char *str)
{
	if (strcmp("ERR", str) == 0)
		return MSG_ERR;
	if (strcmp("WARN", str) == 0)
		return MSG_WARN;
	if (strcmp("INFO", str) == 0)
		return MSG_INFO;
	if (strcmp("DBG", str) == 0)
		return MSG_DBG;
	if (strcmp("TRACE", str) == 0)
		return MSG_TRACE;
	
	return MSG_ERR;
}
void msgobj_init (msgobj *mo, int verbose, int encolor, char *name)
{
	mo->verbose = verbose;
	mo->encolor = encolor;
	mo->name = name;
}
void msgobj_set_verbose (msgobj *mo, int verbose)
{
	mo->verbose = verbose;
}
void msgobj_set_enable_color (msgobj *mo)
{
	mo->encolor = 1;
}
void msgobj_set_name (msgobj *mo, char *name)
{
	mo->name = name;
}

static void _msg_output (msgobj *mo, int type, const char *msg)
{
#define COL(x)  "\033[" #x ";1m"
#define RED     COL(31)
#define GREEN   COL(32)
#define YELLOW  COL(33)
#define BLUE    COL(34)
#define MAGENTA COL(35)
#define CYAN    COL(36)
#define WHITE   COL(37)
#define GRAY    "\033[0m"

	static const char *msg_type_str[] = 
			{"error", "warning", "info", "debug", "trace"};
	static const char *msg_color_str[] = {RED, YELLOW, BLUE, GRAY, CYAN};

	switch (type) {
		case MSG_ERR:
			if (mo->verbose < MSG_ERR) return;
            break;
		case MSG_WARN:
			if (mo->verbose < MSG_WARN) return;
			break;
		case MSG_INFO:
			if (mo->verbose < MSG_INFO) return;
			break;
		case MSG_DBG:
			if (mo->verbose < MSG_DBG) return;
			break;
		case MSG_TRACE:
			if (mo->verbose < MSG_TRACE) return;
			break;
		default:
			return;
	}
    /* Send the message to stderr */
    if (mo->encolor)
    {
        fprintf(stderr, "[" GREEN "%s" GRAY "] %s: %s%s" GRAY "\n",
                         mo->name, msg_type_str[type],
						 msg_color_str[type], msg);
    } else {
		fprintf(stderr, "[%s] %s: %s\n",
			mo->name, msg_type_str[type],
			msg);
    }
}
void msg_output (msgobj *mo, int type, const char *fmt, va_list va)
{
	char *str;
	int rc;

	rc = vasprintf(&str, fmt, va);
	if (rc == -1) {
		va_end(va);
		return;
	}
	_msg_output(mo, type, str);
	free(str);
}
void msg_err (msgobj *mo, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	msg_output(mo, MSG_ERR, fmt, ap);
	va_end(ap);
}
void msg_warn (msgobj *mo, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	msg_output(mo, MSG_WARN, fmt, ap);
	va_end(ap);
}
void msg_info (msgobj *mo, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	msg_output(mo, MSG_INFO, fmt, ap);
	va_end(ap);
}
void msg_dbg (msgobj *mo, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	msg_output(mo, MSG_DBG, fmt, ap);
	va_end(ap);
}
void msg_trace (msgobj *mo, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	msg_output(mo, MSG_TRACE, fmt, ap);
	va_end(ap);
}

void hex_dump(const char *name, unsigned char *buf, int size)
{
	int i;

	if (size <= 0)
		return;

	printf("%s(buf %p, size %d):\n", name, buf, size);
	for (i = 0; i < size; i++) {
		printf("%02x ", *buf);
		if ((i + 1) % 16 == 0)
			putchar('\n');
		buf++;
	}
	printf("\n");
}

