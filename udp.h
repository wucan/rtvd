#ifndef _UDP_H_
#define _UDP_H_

#include <sys/socket.h>
#include <arpa/inet.h>


struct udp_context {
	int sock;
	short port;

	struct sockaddr_in m_addr;
	struct ip_mreq m_imr;
};

struct udp_context * udp_open(char *ip, short port);
void udp_close(struct udp_context *ctx);


#endif /* _UDP_H_ */

