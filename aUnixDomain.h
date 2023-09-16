#ifndef _AUNIXDOMAIN_H
#define _AUNIXDOMAIN_H

#define UN_OK 0
#define UN_ERR -1

#define SOCKET_FILE "/rcmd.sock"
#define BACKLOG 5
#define BUF_SIZE 4096

int aUnixDomainServer();
int aUnixDomainAccept(int servSock);
int aUnixDomainConnect(void);
int aUnixDomainRead(int fd, void *buf, int count);
int aUnixDomainWrite(int fd, void *buf, int count);

#endif	/* _AUNIXDOMAIN_H */
