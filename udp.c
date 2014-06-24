#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE		2048

#define SERV_PORT	6666
#define DEST_IP		"10.0.6.18"
#define DEST_PORT	6666

static int sockfd;

int udp_init()
{
	struct sockaddr_in addr;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(SERV_PORT);

	bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
}

int udp_recv(char *buf, int n)
{
	return recv(sockfd, buf, n, 0);
}

int udp_send(char *buf, int n)
{
	struct sockaddr_in addr;

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(DEST_IP);
	addr.sin_port = htons(DEST_PORT);

	return sendto(sockfd, buf, n, 0, (struct sockaddr *)&addr, sizeof(addr));
}
