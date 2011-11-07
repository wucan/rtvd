/*
 * 
 *
 * Copyright (C) 2005, wucan <can_wu@cnbvcom.com>
 *
 */

#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>

__BEGIN_DECLS


/*
 * Message types
 */ 
/* error messages */
#define MSG_ERR   0
/* warning messages */
#define MSG_WARN  1
/* standard messages */
#define MSG_INFO  2
/* debug messages */
#define MSG_DBG   3
/* trace messages */
#define MSG_TRACE 4

int msg_verbose_str_to_type (char *str);

typedef struct _msgobj {
	/*
	 * msg function enable if verbose > type
	 */
	int verbose;
	int encolor;
	const char *name;
} msgobj;

/* encolor field */
#define ENCOLOR		1
#define NOCOLOR		0

void msgobj_init (msgobj *mo, int verbose, int encolor, char *name);
void msgobj_set_verbose (msgobj *mo, int verbose);
void msgobj_set_enable_color (msgobj *mo);
void msgobj_set_name (msgobj *mo, char *name);

void msg_output (msgobj *mo, int type, const char *fmt, va_list va); 

void msg_err (msgobj *mo, const char *fmt, ...);
void msg_warn (msgobj *mo, const char *fmt, ...);
void msg_info (msgobj *mo, const char *fmt, ...);
void msg_dbg (msgobj *mo, const char *fmt, ...);
void msg_trace (msgobj *mo, const char *fmt, ...);

#define trace_err(fmt, arg...) 		msg_err(&mo, fmt, ## arg)
#define trace_warn(fmt, arg...)		msg_warn(&mo, fmt, ## arg)
#define trace_info(fmt, arg...) 	msg_info(&mo, fmt, ## arg)
#define trace_dbg(fmt, arg...) 		msg_dbg(&mo, fmt, ## arg)
#define trace_trace(fmt, arg...) 	msg_trace(&mo, fmt, ## arg)

void hex_dump(const char *name, unsigned char *buf, int size);

__END_DECLS

#endif /* _MESSAGE_H_ */

