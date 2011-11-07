/*
 * udp
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/uio.h>

#include "udp.h"
#include "message.h"


static msgobj mo = {
	MSG_INFO,
	1,
	"udp",
};

struct udp_context * udp_open(char *ip, short port)
{
#if WINDOWS
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	int dw;
	int ret = 0;
	struct udp_context *ctx;

	ctx = (struct udp_context *)malloc(sizeof(*ctx));
	ctx->port = port;
	memset(&ctx->m_addr, 0, sizeof(ctx->m_addr));
	memset(&ctx->m_imr, 0, sizeof(ctx->m_imr));

	ctx->m_addr.sin_family = AF_INET;
	ctx->m_addr.sin_addr.s_addr = htonl(INADDR_ANY);//inet_addr((char *)host_ip);//htonl(INADDR_ANY);
	ctx->m_addr.sin_port = htons(port);

	ctx->m_imr.imr_multiaddr.s_addr = inet_addr((char *)ip);
	ctx->m_imr.imr_interface.s_addr = INADDR_ANY;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		return NULL;
	}

	dw = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&dw, sizeof(dw)) < 0) {
		close(sock);
		return NULL;
	}

	dw = 128 * 1024;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&dw, sizeof(dw)) < 0) {
		close(sock);
		return NULL;
	}

	if (bind(sock, (struct sockaddr*)&ctx->m_addr, sizeof(struct sockaddr_in)) < 0) {
		close(sock);
		return NULL;
	}

	if (IN_MULTICAST(htonl(ctx->m_imr.imr_multiaddr.s_addr))) {
		trace_info("setup multicast");
		ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				(const char*)&ctx->m_imr, sizeof(ctx->m_imr));
		if (ret < 0) {
			return NULL;
		}
	}
	ctx->sock = sock;
	trace_info("udp context created at %s:%d, sock %d", ip, port, sock);

	return ctx;
}

void udp_close(struct udp_context *ctx)
{
	if (ctx->sock > 0) {
		close(ctx->sock);
	}
	free(ctx);
}

