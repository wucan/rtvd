#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "mongoose.h"
#include "udp.h"


static const char *vlc_http_standard_reply = "HTTP/1.1 200 OK\r\n"
"Content-type: application/octet-stream\r\n"
"Cache-Control: no-cache\r\n\r\n";

#define MAX(a, b)		((a) > (b) ? (a) : (b))

#define MAX_UDP_PROGRAM		100
#define MAX_HTTP_STREAM		100

enum {
	HTTP_STREAM_STATUS_IDLE = 0,
	HTTP_STREAM_STATUS_RUNNING,
	HTTP_STREAM_STATUS_CLOSE,
};

struct http_stream {
	struct mg_connection *conn;
	struct mg_request_info *ri;
	int status;
	int send_bytes;
	time_t start_time;
};

struct udp_program_entry {
	const char *udp_addr;
	struct udp_context *udp_ctx;
	int sock;
	pthread_t thread;

	struct http_stream streams[MAX_HTTP_STREAM];
	int nr_streams;
};

static struct udp_program_entry udp_program_table[MAX_UDP_PROGRAM];

static struct udp_program_entry *
find_udp_program_entry(const char *udp_addr)
{
	int i;

	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		if (udp_program_table[i].udp_addr &&
			!strcmp(udp_program_table[i].udp_addr, udp_addr))
			return &udp_program_table[i];
	}

	return NULL;
}

static struct http_stream *
add_http_stream(struct udp_program_entry *p,
	struct mg_connection *conn, struct mg_request_info *ri)
{
	int i;

	for (i = 0; i < MAX_HTTP_STREAM; i++) {
		if (p->streams[i].status != HTTP_STREAM_STATUS_RUNNING) {
			printf("add http stream in slot #%d of udp program %s\n",
				i, p->udp_addr);
			p->streams[i].send_bytes = 0;
			p->streams[i].start_time = time(NULL);
			p->streams[i].conn = conn;
			p->streams[i].ri = ri;
			p->streams[i].status = HTTP_STREAM_STATUS_RUNNING;
			p->nr_streams = MAX(i + 1, p->nr_streams);
			return &p->streams[i];
		}
	}

	return NULL;
}

#define UDP_PKG_SIZE		(188 * 7)

static int udp_read_data(struct udp_context *udp_ctx, void *buf)
{
	int sock = udp_ctx->sock;
	int from_addr_len = sizeof(struct sockaddr_in);
	int len;
	int rc;
	struct timeval to;
	fd_set read_set;

	to.tv_sec = 1;
	to.tv_usec = 0;
	FD_ZERO(&read_set);
	FD_SET(sock, &read_set);

	rc = select(sock + 1, &read_set, NULL, NULL, &to);
	if (rc > 0) {
		len = recvfrom(sock, buf, UDP_PKG_SIZE, 0,
			(struct sockaddr *)&udp_ctx->m_addr, (socklen_t *)&from_addr_len);
		//hex_dump("udp data", buf, 200);
		return len;
	} else if (rc == 0) {
		// timeout
		return 0;
	} else {
		// error
		return -1;
	}

	// can't reach here!

	return 0;
}

static void * udp_program_thread(void *data)
{
	struct udp_program_entry *p = (struct udp_program_entry *)data;
	int i, rc, len;
	char buf[UDP_PKG_SIZE];

	while (1) {
		len = udp_read_data(p->udp_ctx, buf);
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
		}

		for (i = 0; i < p->nr_streams; i++) {
			if (p->streams[i].conn &&
				p->streams[i].status == HTTP_STREAM_STATUS_RUNNING) {
				//printf("%s: send %d data to slot #%d\n", p->udp_addr, len, i);
				rc = mg_write(p->streams[i].conn, buf, len);
				if (rc <= 0) {
					printf("http stream %s closed!\n", p->udp_addr);
					p->streams[i].status = HTTP_STREAM_STATUS_CLOSE;
					p->streams[i].conn = NULL;
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

	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		if (!udp_program_table[i].udp_addr) {
			udp_program_table[i].udp_addr = 1; // mark it used
			return &udp_program_table[i];
		}
	}

	return NULL;
}

static void put_free_udp_program(struct udp_program_entry *p)
{
	p->udp_addr = 0; // reset it
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

	return 0;
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
	udp_prog = find_udp_program_entry(udp_addr);
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

	mg_printf(conn, "%s", standard_reply);
	mg_printf(conn, "<html><body>");

	mg_printf(conn, "<h2>rtvd version 0.01, support %d udp, %d http per udp</h2><hr>",
		MAX_UDP_PROGRAM, MAX_HTTP_STREAM);
	mg_printf(conn, "<p>stream information:</p>");
	mg_printf(conn, "<table border=\"1\"><tr><th>udp stream</th><th>slot number</th><th>http client</th><th>send bytes</th><th>start time</th></tr>");
	for (i = 0; i < MAX_UDP_PROGRAM; i++) {
		for (j = 0; j < udp_program_table[i].nr_streams; j++) {
			if (udp_program_table[i].streams[j].conn) {
				inaddr.s_addr = htonl(udp_program_table[i].streams[j].ri->remote_ip);
				sprintf(remote, "%s:%d", inet_ntoa(inaddr),
					udp_program_table[i].streams[j].ri->remote_port);
				mg_printf(conn, "<tr><td>%s</td><td>%d</td><td>%s</td><td>%d</td><td>%s</td></tr>",
					udp_program_table[i].udp_addr, j, remote, udp_program_table[i].streams[j].send_bytes, ctime(&udp_program_table[i].streams[j].start_time));
			}
		}
	}
	mg_printf(conn, "</table>");
	mg_printf(conn, "</body></html>");
}

