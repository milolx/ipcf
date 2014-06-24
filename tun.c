#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <net/if.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/if_tun.h>

#define TUN_NAME	"tun77"
#define TUN_IP		"172.16.77.1/24"

int tun_fd;

int tun_alloc(char *dev, int flags)
{
	struct ifreq ifr;
	int fd, err;
	char *clonedev = "/dev/net/tun";

	/* Arguments taken by the function:
	 *
	 * char *dev: the name of an interface (or '\0'). MUST have enough
	 *   space to hold the interface name if '\0' is passed
	 * int flags: interface flags (eg, IFF_TUN etc.)
	 */

	/* open the clone device */
	if( (fd = open(clonedev, O_RDWR)) < 0 ) {
		return fd;
	}

	/* preparation of the struct ifr, of type "struct ifreq" */
	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = flags;   /* IFF_TUN or IFF_TAP, plus maybe IFF_NO_PI */

	if (*dev) {
		/* if a device name was specified, put it in the structure; otherwise,
		 * the kernel will try to allocate the "next" device of the
		 * specified type */
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	/* try to create the device */
	if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
		close(fd);
		return err;
	}

	/* if the operation was successful, write back the name of the
	 * interface to the variable "dev", so the caller can know
	 * it. Note that the caller MUST reserve space in *dev (see calling
	 * code below) */
	strcpy(dev, ifr.ifr_name);

	/* this is the special file descriptor that the caller will use to talk
	 * with the virtual interface */
	return fd;
}

void tun_init()
{
	char tun_name[IFNAMSIZ];
	int nread;
	char cmd[100];

	/* Connect to the device */
	strcpy(tun_name, TUN_NAME);
	tun_fd = tun_alloc(tun_name, IFF_TUN | IFF_NO_PI);  /* tun interface */

	if(tun_fd < 0){
		perror("Allocating interface");
		exit(1);
	}
	sprintf(cmd, "ip addr add %s dev %s", TUN_IP, TUN_NAME);
	system(cmd);
	sprintf(cmd, "ip link set %s up", TUN_NAME);
	system(cmd);
}

int tun_read(char *buf, int n)
{
	return read(tun_fd, buf, n);
}

int tun_write(char *buf, int n)
{
	return write(tun_fd, buf, n);
}

