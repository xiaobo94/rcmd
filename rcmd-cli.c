#if ! defined(__sun)
        /* Prevents ptsname() declaration being visible on Solaris 8 */
#if ! defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 600
#define _XOPEN_SOURCE 600
#endif
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/select.h>

#include "sds.h"
#include "aUnixDomain.h"
#include "dict.h"
#include "adlist.h"

#define RCMD_RETURN_RESULT 1
#define RCMD_NOT_RETURN_RESULT 2

#define MAX_SNAME 500

typedef sds rcmdProc(list *rcmdList, list *execList);
struct rcmdCommand {
  char *name;
  rcmdProc *proc;
  int flags;
};

/* ============================== pty ============================== */

struct termios ttyOrig;

static int ttySetRaw(int fd, struct termios *prevTermios)
{
  struct termios t;
  
  if (tcgetattr(fd, &t) == -1)
    return -1;
  
  if (prevTermios != NULL)
    *prevTermios = t;
  
  t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
  /* Noncanonical mode, disable signals, extended
     input processing, and echoing */
  
  t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
		 INPCK | ISTRIP | IXON | PARMRK);
  /* Disable special handling of CR, NL, and BREAK.
     No 8th-bit stripping or parity error handling.
     Disable START/STOP output flow control. */
  
  t.c_oflag &= ~OPOST;                /* Disable all output processing */
  
  t.c_cc[VMIN] = 1;                   /* Character-at-a-time input */
  t.c_cc[VTIME] = 0;                  /* with blocking */
  
  if (tcsetattr(fd, TCSAFLUSH, &t) == -1)
    return -1;
  
  return 0;
}

static void ttyReset(void)
{
  if (tcsetattr(STDIN_FILENO, TCSANOW, &ttyOrig) == -1) {
    printf("tcsetattr() error\n");
    exit(1);
  }
}

static int ptyMasterOpen(char *slaveName, size_t snLen)
{
  int masterFd, savedErrno;
  char *p;

  masterFd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
  if (masterFd == -1)
    return -1;

  if (grantpt(masterFd) == -1) {
    savedErrno = errno;
    close(masterFd);
    errno = savedErrno;
    return -1;
  }

  if (unlockpt(masterFd) == -1) {
    savedErrno = errno;
    close(masterFd);
    errno = savedErrno;
    return -1;
  }

  p = ptsname(masterFd);
  if (p == NULL) {
    savedErrno = errno;
    close(masterFd);
    errno = savedErrno;
    return -1;
  }

  if (strlen(p) < snLen) {
    strncpy(slaveName, p, snLen);
  } else {
    close(masterFd);
    errno = EOVERFLOW;
    return -1;
  }
  return masterFd;
}

/* ============================== Prototypes ============================== */

static void parseArgs(int argc, char **argv, list* rcmdList, list* execList);
static int startsWith(char *s, char *s1);
static sds run(list *execList);
static sds saddCommand(list *rcmdList, list *execList);
static sds sgetCommand(list *rcmdList, list *execList);
static sds saveCommand(list *rcmdList, list *execList);
static sds sdelCommand(list *rcmdList, list *execList);
static sds allCmdCommand(list *rcmdList, list *execList);
//static sds quitCommand(list *rcmdList, list *execList);

static struct rcmdCommand cmdTable[] = {
  {"sadd", saddCommand, RCMD_RETURN_RESULT},
  {"sget", sgetCommand, RCMD_RETURN_RESULT},
  {"save", saveCommand, RCMD_RETURN_RESULT},
  {"sdel", sdelCommand, RCMD_RETURN_RESULT},
  {"allcmd", allCmdCommand, RCMD_RETURN_RESULT},
  //  {"quit", quitCommand, RCMD_RETURN_RESULT},
  {NULL, NULL, 0}
};

/* ============================== Functions ============================== */

static sds saddCommand(list *rcmdList, list *execList)
{
  sds cmd = sdsEmpty(), execcmd = sdsEmpty();
  unsigned int j;
  
  for (j = 0; j < listLength(rcmdList); j++) {
    //if (j != 0) cmd = sdsCat(cmd, " ");
    cmd = sdsCatLen(cmd, (char*)listNodeValue(listIndex(rcmdList, j)), strlen((char*)listNodeValue(listIndex(rcmdList, j))));
    cmd = sdsCat(cmd, " ");
  }

  sds ret = run(execList);
  listAddNodeTail(execList, "\r\n");
  listAddNodeTail(execList, ret);
  
  for (j = 0; j < listLength(execList); j++) {
    if (j != 0 && j < listLength(execList) - 2) execcmd = sdsCat(execcmd, " ");
    execcmd = sdsCatLen(execcmd, (char*)listNodeValue(listIndex(execList, j)), strlen((char*)listNodeValue(listIndex(execList, j))));
  }
  execcmd = sdsCat(execcmd, "\r\n");
  cmd = sdsCatPrintf(cmd, "%d", sdsLen(execcmd));
  cmd = sdsCat(cmd, "\r\n");
  cmd = sdsCat(cmd, execcmd);
  sdsFree(execcmd);
  sdsFree(ret);
  return cmd;
}

static sds sgetCommand(list *rcmdList, list *execList)
{
  sds cmd = sdsEmpty();
  unsigned int j;
  for (j = 0; j < listLength(rcmdList); j++) {
    //if (j != 0) cmd = sdsCat(cmd, " ");
    cmd = sdsCatLen(cmd, (char*)listNodeValue(listIndex(rcmdList, j)), strlen((char*)listNodeValue(listIndex(rcmdList, j))));
    cmd = sdsCat(cmd, " ");
  }
  
  char* soft = listNodeValue(listFirst(execList));

  cmd = sdsCatPrintf(cmd, "%d", strlen(soft));
  cmd = sdsCat(cmd, "\r\n");
  cmd = sdsCat(cmd, soft);
  cmd = sdsCat(cmd, "\r\n\0");
  return cmd;
}

static sds saveCommand(list *rcmdList, list *execList)
{
  if (listLength(rcmdList) != 1 || listLength(execList) != 0) {
    fprintf(stderr, "save command do not need args\n");
    exit(1);
  }
  sds cmd = sdsNew("save 2\r\n\r\n");

  return cmd;
}

static sds sdelCommand(list *rcmdList, list *execList)
{
  sds cmd = sdsEmpty(), execcmd = sdsEmpty();
  unsigned int j;
  for (j = 0; j < listLength(rcmdList); j++) {
    cmd = sdsCatLen(cmd, (char*)listNodeValue(listIndex(rcmdList, j)), strlen((char*)listNodeValue(listIndex(rcmdList, j))));
    cmd = sdsCat(cmd, " ");
  }

  for (j = 0; j < listLength(execList); j++) {
    if (j != 0 && j < listLength(execList))
      execcmd = sdsCat(execcmd, " ");
    execcmd = sdsCatLen(execcmd, (char*)listNodeValue(listIndex(execList, j)), strlen((char*)listNodeValue(listIndex(execList, j))));
  }
  execcmd = sdsCat(execcmd, "\r\n");
  cmd = sdsCatPrintf(cmd, "%d", sdsLen(execcmd));
  cmd = sdsCat(cmd, "\r\n");
  cmd = sdsCat(cmd, execcmd);
  sdsFree(execcmd);
  return cmd;
}

static sds allCmdCommand(list *rcmdList, list *execList)
{
  if (listLength(rcmdList) != 1 || listLength(execList) != 0) {
    fprintf(stderr, "allcmd command do not need args\n");
    exit(1);
  }
  sds cmd = sdsNew("allcmd 2\r\n\r\n");

  return cmd;
}

/*
static sds quitCommand(list *rcmdList, list *execList)
{
  if (listLength(rcmdList) != 1 || listLength(execList) != 0) {
    fprintf(stderr, "quit command do not need args\n");
    exit(1);
  }
  sds cmd = sdsNew("quit 2\r\n\r\n");

  return cmd;
}
*/

static int startsWith(char *s, char *s1)
{
  size_t len = strlen(s1);
  if (strlen(s) >= len && strncmp(s, s1, len) == 0)
    return 1;
  return 0;
}

static void parseArgs(int argc, char **argv, list* rcmdList, list* execList)
{
  int execCMD = 0;
  listAddNodeTail(rcmdList, argv[1]);
  int i;

  for (i = 2; i < argc; i++) {
    if (startsWith(argv[i], "-") && execCMD == 0) {
      listAddNodeTail(rcmdList, argv[i]);
      listAddNodeTail(rcmdList, argv[++i]);
    } else {
      execCMD = 1;
      listAddNodeTail(execList, argv[i]);
    }
  }
  return;
}

static sds run(list *execList)
{
  char slaveName[MAX_SNAME] = {'\0'};
  struct winsize ws;
  fd_set inFds;
  char buf[BUF_SIZE];
  int nread, masterFd, slaveFd;
  pid_t pid;
  
  if (tcgetattr(STDIN_FILENO, &ttyOrig) == -1) {
    printf("tcgetattr() error\n");
    exit(1);
  }
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0) {
    printf("ioctl() error\n");
    exit(1);
  }

  masterFd = ptyMasterOpen(&slaveName, MAX_SNAME);
  if (masterFd == -1) {
    printf("ptyMasterOpen() error\n");
    exit(1);
  }
  if (slaveName[0] == '\0') {
    close(masterFd);
    printf("ptyMasterOpen() error\n");
    exit(1);
  }

  pid = fork();

  if (pid == -1) {
    printf("fork() error\n");
    exit(1);
  }

  if (pid == 0) {		/* child process */
    if (setsid() == -1) {
      printf("setsid() error\n");
      exit(1);
    }

    close(masterFd);

    slaveFd = open(slaveName, O_RDWR);
    if (slaveFd == -1) {
      printf("open-slave error\n");
      exit(1);
    }

    if (ioctl(slaveFd, TIOCSCTTY, 0) == -1) {
      printf("ioctl-TIOCSCTTY error\n");
      exit(1);
    }

    if (tcsetattr(slaveFd, TCSANOW, &ttyOrig) == -1) {
      printf("tcsetattr() error\n");
      exit(1);
    }

    if (ioctl(slaveFd, TIOCSWINSZ, &ws) == -1) {
      printf("ioctl-TIOCSWINZ error\n");
      exit(1);
    }

    if (dup2(slaveFd, STDIN_FILENO) != STDIN_FILENO) {
      printf("dup2-STDIN_FILENO error\n");
      exit(1);
    }

    if (dup2(slaveFd, STDOUT_FILENO) != STDOUT_FILENO) {
      printf("dup2-STDOUT_FILENO error\n");
      exit(1);
    }

    if (dup2(slaveFd, STDERR_FILENO) != STDERR_FILENO) {
      printf("dup2-STDERR_FILENO error\n");
      exit(1);
    }

    if (slaveFd > STDERR_FILENO)
      close(slaveFd);

    char **args = malloc(sizeof(char *) * (listLength(execList) + 1)); 
    unsigned int i; 
    
    for (i = 0; i < listLength(execList); i++) 
      args[i] = (char*)listNodeValue(listIndex(execList, i)); 
    args[i] = NULL; 
    
    if ((execvp(args[0], args)) == -1) { 
      printf("%s", strerror(errno)); 
      exit(1); 
    }
  }

  ttySetRaw(STDIN_FILENO, &ttyOrig);
  
  if (atexit(ttyReset) != 0) {
    printf("atexit() error\n");
    exit(1);
  }

  sds retChar = sdsEmpty();
  
  while (1) {
    FD_ZERO(&inFds);
    FD_SET(STDIN_FILENO, &inFds);
    FD_SET(masterFd, &inFds);

    if (select(masterFd + 1, &inFds, NULL, NULL, NULL) == -1) {
      printf("select() error\n");
      exit(1);
    }

    if (FD_ISSET(STDIN_FILENO, &inFds)) {
      nread = read(STDIN_FILENO, buf, BUF_SIZE);
      if (nread <= 0)
	break;

      if (write(masterFd, buf, nread) != nread) {
	printf("write() error\n");
	exit(1);
      }
    }

    if (FD_ISSET(masterFd, &inFds)) {
      nread = read(masterFd, buf, BUF_SIZE);
      if (nread <= 0)
	break;

      if (write(STDOUT_FILENO, buf, nread) != nread) {
	printf("write-STDOUT_FILENO error\n");
	exit(1);
      }
      retChar = sdsCatLen(retChar, buf, nread);
    }
  }

  return retChar;
}

/*
static sds run(list *execList)
{
  int fds[2];
  pid_t pid;
  sds retChar = sdsEmpty();
  
  pipe(fds);
  if ((pid = fork()) == 0) {
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    char **args = malloc(sizeof(char *) * (listLength(execList) + 1));
    unsigned int i;

    for (i = 0; i < listLength(execList); i++)
      args[i] = (char*)listNodeValue(listIndex(execList, i));
    args[i] = NULL;

    if ((execvp(args[0], args)) == -1) {
      printf("%s", strerror(errno));
      exit(1);
    }
  } else {
    fd_set rfds;
    int status, nread = 0;
    char buf[BUF_SIZE];
    buf[nread] = '\0';
    struct timeval tv;

    FD_ZERO(&rfds);
    while (1) {
      tv.tv_sec = 0;
      tv.tv_usec = 1000;
      FD_SET(fds[0], &rfds);
      if (select(fds[0] + 1, &rfds, NULL, NULL, &tv) > 0) {
	nread = read(fds[0], buf, BUF_SIZE);
	buf[nread] = '\0';
     	printf("%s", buf);
	retChar = sdsCat(retChar, buf);
      }
      if (waitpid(pid, &status, WNOHANG) && nread != BUF_SIZE) {
	if (WEXITSTATUS(status)) {
	  printf("\n\033[46;31mrcmd: notice\033[0m: The command exit code %d is not equal to 0, exit.\n", WEXITSTATUS(status));
	  exit(0);
	}
	break;
      }
    }
  }
  return retChar;
}
*/

static struct rcmdCommand *lookupCommand(char *name)
{
  int j = 0;
  while (cmdTable[j].name != NULL) {
    if (!strcasecmp(name, cmdTable[j].name))
      return &cmdTable[j];
    j++;
  }
  return NULL;
}

/*
static int cliConnect(void)
{
  int servfd;
  servfd = aUnixDomainConnect();
  return servfd;  
}
*/

static sds cliReadLine(int fd)
{
  sds line = sdsEmpty();

  while (1) {
    char c;

    if (read(fd, &c, 1) == -1) {
      sdsFree(line);
      return NULL;
    } else if (c == '\n') {
      break;
    } else {
      line = sdsCatLen(line, &c, 1);
    }
  }

  return sdsTrimSet(line, "\r\n");
}

int cliReadInlineReply(int fd)
{
  sds reply = cliReadLine(fd);

  if (reply == NULL) return 1;
  int num = atoi(reply), nread;
  char buf[BUF_SIZE];
  reply = sdsEmpty();

  while (num) {
    if (num > BUF_SIZE) {
      nread = read(fd, buf, BUF_SIZE);
      reply = sdsCatLen(reply, buf, nread);
      num -= nread;
    } else if (num > 0) {
      nread = read(fd, buf, num);
      reply = sdsCatLen(reply, buf, nread);
      num -= nread;
    } else {
      return 1;
    }
  }

  printf("%s", reply);
  sdsFree(reply);

  return 0;
}

int cliSendCommand(list* rcmdList, list* execList)
{
  char *soft = listNodeValue(listFirst(rcmdList));
  struct rcmdCommand *rc = lookupCommand(soft);
  int servfd, retval = 0;
  sds cmd;
  
  if (!rc) {
    fprintf(stderr, "Unknown command '%s'.\n", soft);
    return 1;
  }

  if ((servfd = aUnixDomainConnect()) == -1) return 1;

  cmd = rc->proc(rcmdList, execList);
  
  aUnixDomainWrite(servfd, cmd, sdsLen(cmd));

  retval = cliReadInlineReply(servfd);
  
  sdsFree(cmd);
  close(servfd);
  return retval;
}

int main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(stderr, "The arg is too less.\n");
    exit(1);
  }

  list *rcmdList = listCreate();
  list *execList = listCreate();
  parseArgs(argc, argv, rcmdList, execList);

  /*
  sds ret = run(execList);
  listAddNodeTail(execList, "\r\n");
  listAddNodeTail(execList, ret); 
  */
  
  return cliSendCommand(rcmdList, execList);
}
