#include <sys/un.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "aUnixDomain.h"
#include "sds.h"

static void errExit(char *message)
{
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}

int aUnixDomainServer()
{
  struct sockaddr_un addr;
  int servfd;
  char *home;
  sds socketFile;

  home = getenv("HOME");
  socketFile = sdsNew(home);
  socketFile = sdsCat(socketFile, SOCKET_FILE);
  //  socketFile = sdsNew(SOCKET_FILE);

  servfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (servfd == -1)
    errExit("socket() error");

  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(&addr.sun_path[1], socketFile, sizeof(addr.sun_path) - 2);
  sdsFree(socketFile);

  if (bind(servfd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == -1)
    errExit("bind() error");

  if (listen(servfd, BACKLOG) == -1)
    errExit("listen() error");

  return servfd;
}

int aUnixDomainAccept(int servSock)
{
  int clntSock;

  clntSock = accept(servSock, NULL, NULL);
  if (clntSock == -1)
    errExit("accept() error");

  return clntSock;
}
/*
int aUnixDomainConnect(void)
{
  struct sockaddr_un addr;
  int servSock;
  sds sockFile;

  sockFile = sdsNew(SOCKET_FILE);

  servSock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (servSock == -1) 
    errExit("socket() error");

  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(&addr.sun_path[1], sockFile, sizeof(addr.sun_path) - 2);

  if (connect(servSock, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
    fprintf(stderr, "connect() error\n");
    exit(1);
  }
  return servSock;
}
*/
int aUnixDomainConnect(void)
{
  struct sockaddr_un addr;
  int servfd;
  char *home;
  sds sockFile;

  home = getenv("HOME");
  sockFile = sdsNew(home);
  sockFile = sdsCat(sockFile, SOCKET_FILE);
  //  sockFile = sdsNew(SOCKET_FILE);

  servfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (servfd == -1)
    errExit("socket() error");

  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(&addr.sun_path[1], sockFile, sizeof(addr.sun_path) - 2);
  sdsFree(sockFile);

  if (connect(servfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
    errExit("connect() error");

  return servfd;
}

int aUnixDomainRead(int fd, void *buf, int count)
{
  int nread, totlen = 0;
  while (totlen != count) {
    nread = read(fd, buf, count - totlen);
    if (nread == 0) return totlen;
    if (nread == -1) return -1;
    totlen += nread;
    buf += nread;
  }
  return totlen;
}

int aUnixDomainWrite(int fd, void *buf, int count)
{
  int nwritten, totlen = 0;
  while (totlen != count) {
    nwritten = write(fd, buf, count - totlen);
    if (nwritten == 0) return totlen;
    if (nwritten == -1) return -1;
    totlen += nwritten;
    buf += nwritten;
  }
  return totlen;
}
