#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#include "mongoose.h"
#include "udp.h"
#include "rtvd.h"


static const char *vlc_http_standard_reply = "HTTP/1.1 200 OK\r\n"
"Content-type: application/octet-stream\r\n"
"Cache-Control: no-cache\r\n\r\n";

#define MAX(a, b)		((a) > (b) ? (a) : (b))

#define MAX_UDP_PROGRAM		100
#define MAX_HTTP_STREAM		100
#define MAX_UDP_IDLE_TIME	10
#define MAX_RATE_SEC		(1 << 6)

enum {
	HTTP_STREAM_STATUS_IDLE = 0,
	HTTP_STREAM_STATUS_RUNNING,
	HTTP_STREAM_STATUS_CLOSE,
};


struct pid_info {
	uint32_t count;

	uint16_t rate_count;
	uint16_t rate_history[MAX_RATE_SEC]
};

struct http_stream {
	struct mg_connection *conn;
	struct mg_request_info *ri;
	int status;
	int send_bytes;
	int discard_bytes;
	time_t start_time;
};

struct udp_program_entry {
	int refcnt;
	const char *udp_addr;
	struct udp_context *udp_ctx;
	int sock;
	pthread_t thread;

	pthread_mutex_t mutex;
	struct http_stream streams[MAX_HTTP_STREAM];
	int max_stream_index;
	int nr_streams;
	int nr_users;

	time_t idle_start_time;

	struct pid_info pid_table[0x1FFF + 1];
	uint16_t rate_index;
};

static struct udp_program_entry udp_program_table[MAX_UDP_PROGRAM];
static pthread_mutex_t prog_mutex = PTHREAD_MUTEX_INITIALIZER;

static int udp_program_destroy(struct udp_program_entry *p);

static struct udp_program_entry *
get_udp_program(const char *udp_addr)
{
	int i;
	struct udp_program_entry *p = NULL;

	pthread_mutex_lock(&prog_mutex);
	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		if (udp_program_table[i].udp_addr &&
			!strcmp(udp_program_table[i].udp_addr, udp_addr)) {
			p = &udp_program_table[i];
			p->refcnt++;
			break;
		}
	}
	pthread_mutex_unlock(&prog_mutex);

	return p;
}

static struct udp_program_entry * get_first_udp_program()
{
	int i;
	struct udp_program_entry *p = NULL;

	pthread_mutex_lock(&prog_mutex);
	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		if (udp_program_table[i].udp_addr &&
			udp_program_table[i].udp_addr != 1) {
			p = &udp_program_table[i];
			p->refcnt++;
			break;
		}
	}
	pthread_mutex_unlock(&prog_mutex);

	return p;
}

static void put_udp_program(struct udp_program_entry *p)
{
	pthread_mutex_lock(&prog_mutex);
	p->refcnt--;
	pthread_mutex_unlock(&prog_mutex);
}

static void inc_udp_program_user(struct udp_program_entry *p)
{
	pthread_mutex_lock(&p->mutex);
	p->nr_users++;
	pthread_mutex_unlock(&p->mutex);
}

static void dec_udp_program_user(struct udp_program_entry *p)
{
	pthread_mutex_lock(&p->mutex);
	if (p->nr_users == 1)
		p->idle_start_time = time(NULL);
	if (p->nr_users > 0)
		p->nr_users--;
	pthread_mutex_unlock(&p->mutex);
}

static struct http_stream *
add_http_stream(struct udp_program_entry *p,
	struct mg_connection *conn, struct mg_request_info *ri)
{
	int i;
	struct http_stream *s = NULL;

	pthread_mutex_lock(&p->mutex);
	for (i = 0; i < MAX_HTTP_STREAM; i++) {
		if (p->streams[i].status != HTTP_STREAM_STATUS_RUNNING) {
			printf("add http stream in slot #%d of udp program %s\n",
				i, p->udp_addr);
			mg_set_non_blocking_mode(conn);
			p->streams[i].send_bytes = 0;
			p->streams[i].discard_bytes = 0;
			p->streams[i].start_time = time(NULL);
			p->streams[i].conn = conn;
			p->streams[i].ri = ri;
			p->streams[i].status = HTTP_STREAM_STATUS_RUNNING;
			p->max_stream_index = MAX(i, p->max_stream_index);
			p->nr_streams++;
			s = &p->streams[i];
			break;
		}
	}
	pthread_mutex_unlock(&p->mutex);

	return s;
}

static void remove_http_stream(struct udp_program_entry *p,
		struct http_stream *s)
{
	pthread_mutex_lock(&p->mutex);
	s->status = HTTP_STREAM_STATUS_CLOSE;
	s->conn = NULL;
	p->nr_streams--;
	pthread_mutex_unlock(&p->mutex);
}

#define UDP_PKG_SIZE		(188 * 7)

static void * udp_program_thread(void *data)
{
	struct udp_program_entry *p = (struct udp_program_entry *)data;
	int i, rc, len;
	unsigned char buf[UDP_PKG_SIZE];
	time_t last_rate_time = 0;

	pthread_detach(pthread_self());
	p->idle_start_time = time(NULL);
	while (1) {
		/*
		 * check for this udp quiting
		 */
		if (p->nr_streams <= 0 && p->nr_users <= 0) {
			if (time(NULL) >= p->idle_start_time + MAX_UDP_IDLE_TIME) {
				printf("%s: quit\n", p->udp_addr);
				if (udp_program_destroy(p))
					pthread_exit(NULL);
			}
			usleep(100000);
			continue;
		}

		len = udp_read_data(p->udp_ctx, buf, UDP_PKG_SIZE);
		if (len <= 0) {
			//printf("send out last data\n");
			memset(buf, 0xFF, UDP_PKG_SIZE);
			for (i = 0; i < UDP_PKG_SIZE; i += 188) {
				buf[i + 0] = 0x47;
				buf[i + 1] = 0x1F;
				buf[i + 2] = 0xFF;
				buf[i + 3] = 0x00;
			}
			len = UDP_PKG_SIZE;
		} else {
			/*
			 * track pid info
			 */
			time_t t = time(NULL);
			for (i = 0; i < UDP_PKG_SIZE; i += 188) {
				uint16_t pid = ((buf[i + 1] & 0x1F) << 8) | buf[i + 2];
				p->pid_table[pid].count++;
				p->pid_table[pid].rate_history[p->rate_index]++;
			}

			/* update rate time/index */
			if (last_rate_time) {
				if (t != last_rate_time) {
					if (++p->rate_index >= MAX_RATE_SEC)
						p->rate_index = 0;
					for (i = 0; i <= 0x1FFF; i++)
						p->pid_table[i].rate_history[p->rate_index] = 0;
					last_rate_time = t;
				}
			} else {
				last_rate_time = t;
			}
		}

		for (i = 0; i <= p->max_stream_index; i++) {
			if (p->streams[i].conn &&
				p->streams[i].status == HTTP_STREAM_STATUS_RUNNING) {
				//printf("%s: send %d data to slot #%d\n", p->udp_addr, len, i);
				rc = mg_write(p->streams[i].conn, buf, len);
				if (rc <= 0) {
					if (errno == EAGAIN) {
						p->streams[i].discard_bytes += len;
						continue;
					}
					printf("http stream %s closed!\n", p->udp_addr);
					remove_http_stream(p, &p->streams[i]);
					if (p->nr_streams <= 0) {
						p->idle_start_time = time(NULL);
						printf("%s: idle start time %s\n",
							p->udp_addr, ctime(&p->idle_start_time));
					}
				}
				p->streams[i].send_bytes += len;
			}
		}
	}

	return NULL;
}

static struct udp_program_entry * get_free_udp_program()
{
	int i;
	struct udp_program_entry *p = NULL;

	pthread_mutex_lock(&prog_mutex);
	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		if (!udp_program_table[i].udp_addr) {
			udp_program_table[i].udp_addr = 1; // mark it used
			p = &udp_program_table[i];
			p->refcnt++;
			break;
		}
	}
	pthread_mutex_unlock(&prog_mutex);

	return p;
}

static void put_free_udp_program(struct udp_program_entry *p)
{
	pthread_mutex_lock(&prog_mutex);
	p->udp_addr = 0; // reset it
	p->refcnt--;
	pthread_mutex_unlock(&prog_mutex);
}

static int udp_program_init(struct udp_program_entry *p, const char *udp_addr)
{
	int rc;
	pthread_t thr;
	char *ip = strdup(udp_addr);
	char *delim;
	short port;

	memset(p, 0, sizeof(*p));

	/* open udp socket */
	delim = strchr(ip, ':');
	if (!delim) {
		free(ip);
		return -1;
	}
	*delim = 0;
	port = atoi(delim + 1);

	p->udp_ctx = udp_open(ip, port);
	if (!p->udp_ctx) {
		printf("udp create failed!\n");
		return -1;
	}
	p->sock = p->udp_ctx->sock;

	/* start thread */
	rc = pthread_create(&thr, NULL, udp_program_thread, p);
	if (rc) {
		perror("pthread_create");
		return -1;
	}
	p->thread = thr;
	p->udp_addr = strdup(udp_addr);
	pthread_mutex_init(&p->mutex, NULL);
	p->refcnt = 1;

	return 0;
}

static int udp_program_destroy(struct udp_program_entry *p)
{
	pthread_mutex_lock(&prog_mutex);
	if (p->refcnt > 1) {
		pthread_mutex_unlock(&prog_mutex);
		return 0;
	}

	udp_close(p->udp_ctx);
	free(p->udp_addr);
	pthread_mutex_destroy(&p->mutex);
	memset(p, 0, sizeof(*p));
	pthread_mutex_unlock(&prog_mutex);

	return 1;
}

void stream_page_handler(struct mg_connection *conn,
			const struct mg_request_info *ri, void *data)
{
	mg_printf(conn, "%s", vlc_http_standard_reply);
	struct udp_program_entry *udp_prog;
	struct http_stream *http_stream = NULL;
	int rc;
	char *udp_addr;

	/*
	 * get udp address
	 */
	udp_addr = mg_get_var(conn, "udp");
	if (udp_addr)
		printf("program udp address: %s\n", udp_addr);
	else {
		printf("no udp address provide!\n");
		return;
	}

	/*
	 * find/create udp_program_entry
	 */
	udp_prog = get_udp_program(udp_addr);
	if (!udp_prog) {
		udp_prog = get_free_udp_program();
		rc = udp_program_init(udp_prog, udp_addr);
		if (rc) {
			put_free_udp_program(udp_prog);
			printf("udp_program init failed!\n");
			return;
		}
	}

	/*
	 * put this http connection to udp_program_entry and playing
	 */
	http_stream = add_http_stream(udp_prog, conn, ri);
	put_udp_program(udp_prog);
	if (http_stream) {
		while (http_stream->status == HTTP_STREAM_STATUS_RUNNING) {
			sleep(1);
		}
		printf("http connection %d:%d done\n", ri->remote_ip, ri->remote_port);
	}
}

static const char *standard_reply = "HTTP/1.1 200 OK\r\n"
"Conntent-Type: text/html\r\n"
"Connection: close\r\n\n";

void stream_info_handler(struct mg_connection *conn,
                   const struct mg_request_info *ri, void *data)
{
	int i, j;
	char remote[64];
	struct in_addr inaddr;
	struct udp_program_entry *p;
	struct http_stream *s;

	mg_printf(conn, "%s", standard_reply);
	mg_printf(conn, "<html><body>");

	mg_printf(conn, "<h2>rtvd version %s, support %d udp, %d http per udp</h2><hr>",
		RTVD_VERSION, MAX_UDP_PROGRAM, MAX_HTTP_STREAM);
	mg_printf(conn, "<p>stream information:</p>");
	mg_printf(conn, "<table border=\"1\"><tr><th>udp stream</th><th>slot number</th><th>http client</th><th>send/discard bytes</th><th>start time</th></tr>");
	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		p = &udp_program_table[i];
		for (j = 0; j <= udp_program_table[i].max_stream_index; j++) {
			s = &p->streams[j];
			if (s->conn) {
				inaddr.s_addr = htonl(s->ri->remote_ip);
				sprintf(remote, "%s:%d", inet_ntoa(inaddr),
					s->ri->remote_port);
				mg_printf(conn, "<tr><td>%s</td><td>%d</td><td>%s</td><td>%d/%d</td><td>%s</td></tr>",
					p->udp_addr, j, remote, s->send_bytes, s->discard_bytes, ctime(&s->start_time));
			}
		}
	}
	mg_printf(conn, "</table>");

	int off = 0;
	char pid_info[1024];
	mg_printf(conn, "<p>pid information:</p>");
	mg_printf(conn,
		"<table border=\"1\"><tr><th>udp stream</th><th>pid</th></tr>");
	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		p = &udp_program_table[i];
		if (p->nr_streams) {
			for (j = 0; j <= 0x1FFF; j++) {
				if (p->pid_table[j].count) {
					off += sprintf(pid_info + off, "%d:%d ",
						j,
						p->pid_table[j].count);
				}
			}
			mg_printf(conn, "<tr><td>%s</td><td>%s</td></tr>",
				p->udp_addr, pid_info);
		}
	}
	mg_printf(conn, "</table>");

	mg_printf(conn, "</body></html>");
}

static const char *svg_standard_reply = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/xml\r\n"
"Connection: close\r\n\n";

void stream_static_handler(struct mg_connection *conn,
                   const struct mg_request_info *ri, void *data)
{
	static char sbuf[1024 * 40];
	struct udp_program_entry *p = NULL;
	int his_idx, y = 60, off = 0, pid;
	int rate_index;
	time_t base_time;
	char *udp_addr;

	/*
	 * which udp program entry to check
	 */
	udp_addr = mg_get_var(conn, "udp");
	if (udp_addr) {
		p = get_udp_program(udp_addr);
	}
	if (!p)
		p = get_first_udp_program();
	if (!p)
		return;

	rate_index = p->rate_index;
	base_time = time(NULL) - rate_index;

	if (rate_index <= 2) {
		put_udp_program(p);
		return;
	}

	mg_printf(conn, "%s", svg_standard_reply);

	off += sprintf(sbuf + off,
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<!DOCTYPE svg>"
		"<svg width=\"800px\" height=\"600px\" xmlns=\"http://www.w3.org/2000/svg\"><g>");

	off += sprintf(sbuf + off,
		"<text font-size=\"16\" x=\"10\" y=\"20\">base time: %s</text>",
		ctime(&base_time));
	for (pid = 0; pid <= 0x1FFF; pid++) {
		if (!p->pid_table[pid].count)
			continue;

		/* pid and timeline */
		off += sprintf(sbuf + off,
			"<text font-size=\"16\" x=\"5\" y=\"%d\">%d</text>",
			y - 2, pid);
		off += sprintf(sbuf + off,
			"<rect x=\"40\" y=\"%d\" width=\"600\" height=\"2\" style=\"fill:#00ff00\" />",
			y);

		int x = 50;
		uint32_t rate_sum = 0;
		for (his_idx = 0; his_idx < rate_index; his_idx++) {
			int r = p->pid_table[pid].rate_history[his_idx];
			rate_sum += r;
			if (r >= 60) {
				int z = r / 60;
				char *z_style = "style=\"fill:#880000\"";
				if (z >= 60)
					z_style = "style=\"fill:#FF0000\"";
				off += sprintf(sbuf + off,
					"<rect x=\"%d\" y=\"%d\" width=\"3\" height=\"%d\" style=\"fill:#AAAAAA\" />",
					x, y -  r % 60, r % 60);
				off += sprintf(sbuf + off,
				"<rect x=\"%d\" y=\"%d\" width=\"1\" height=\"%d\" %s />",
					x + 1, y -  z % 60, z % 60, z_style);
			} else {
				off += sprintf(sbuf + off,
					"<rect x=\"%d\" y=\"%d\" width=\"3\" height=\"%d\" />",
					x, y - r, r);
			}
			x += 5;
		}
		uint32_t rate_avg = rate_sum / rate_index;
		off += sprintf(sbuf + off,
			"<text font-size=\"16\" x=\"%d\" y=\"%d\">avg=%d bps</text>",
			50 + (5 * MAX_RATE_SEC), y - 2, rate_avg * 188 * 8);

		y += 60 + 10;
	}

	put_udp_program(p);

	off += sprintf(sbuf + off, "</g></svg>");

	mg_write(conn, sbuf, off);
}

void stream_pcr_handler(struct mg_connection *conn,
						const struct mg_request_info *ri, void *data)
{
	mg_printf(conn, "%s", standard_reply);
	mg_printf(conn, "<html><head>");
	mg_printf(conn, "<script src=\"js/jquery.js\"></script>");
	mg_printf(conn, "<script src=\"js/pcr.js\"></script>");
	mg_printf(conn, "</head><body>");
	mg_printf(conn, "<div id=\"flipboard\"></div>");
	mg_printf(conn, "<div id=\"error\"></div>");
	mg_printf(conn, "<button id=\"ss_button\">Start</button>");
	mg_printf(conn, "</body></html>");
}

static const char *ajax_reply_start =
	"HTTP/1.1 200 0K\r\n"
	"Cache: no-cache\r\n"
	"Content-Type: application/x-javascript\r\n"
	"\r\n";

static void get_qsvar(const struct mg_request_info *request_info,
				const char *name, char *dst, size_t dst_len)
{
	const char *qs = request_info->query_string;
	mg_get_var_3(qs, strlen(qs == NULL ? "" : qs), name, dst, dst_len);
}

// If "callback" param is present in query string, this is JSONP call.
// Return 1 in this case, or 0 if "callback" is not specified.
// Wrap an output in Javascript function call.
static int handle_jsonp(struct mg_connection *conn,
			const struct mg_request_info *request_info)
{
	char cb[64];

	get_qsvar(request_info, "callback", cb, sizeof(cb));
	if (cb[0] != '\0') {
		mg_printf(conn, "%s(", cb);
	}

	return cb[0] == '\0' ? 0 : 1;
}

void stream_start_flow_handler(struct mg_connection *conn,
						const struct mg_request_info *ri, void *data)
{
	int is_jsonp;
	char udp[128];
	struct udp_program_entry *udp_prog;

	/* response header */
	mg_printf(conn, "%s", ajax_reply_start);
	is_jsonp = handle_jsonp(conn, ri);

	/* start udp program if needed */
	get_qsvar(ri, "udp", udp, sizeof(udp));
	udp_prog = get_udp_program(udp);
	if (!udp_prog) {
		udp_prog = get_free_udp_program();
		int rc = udp_program_init(udp_prog, udp);
		if (rc) {
			put_free_udp_program(udp_prog);
			goto error_out;
		}
	}
	inc_udp_program_user(udp_prog);
	put_udp_program(udp_prog);

error_out:
	/* response tail */
	if (is_jsonp) {
		mg_printf(conn, "%s", ")");
	}
}

void stream_stop_flow_handler(struct mg_connection *conn,
						const struct mg_request_info *ri, void *data)
{
	int is_jsonp;
	char udp[128];
	struct udp_program_entry *udp_prog;

	/* response header */
	mg_printf(conn, "%s", ajax_reply_start);
	is_jsonp = handle_jsonp(conn, ri);

	/* check udp program presened and stop it */
	get_qsvar(ri, "udp", udp, sizeof(udp));
	udp_prog = get_udp_program(udp);
	if (udp_prog) {
		dec_udp_program_user(udp_prog);
		put_udp_program(udp_prog);
	}

	/* response tail */
	if (is_jsonp) {
		mg_printf(conn, "%s", ")");
	}
}

