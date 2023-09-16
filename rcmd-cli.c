#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "sds.h"
#include "aUnixDomain.h"
#include "dict.h"
#include "adlist.h"

#define RCMD_RETURN_RESULT 1
#define RCMD_NOT_RETURN_RESULT 2

typedef sds rcmdProc(list *rcmdList, list *execList);
struct rcmdCommand {
  char *name;
  rcmdProc *proc;
  int flags;
};

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
  
  for (unsigned int j = 0; j < listLength(rcmdList); j++) {
    //if (j != 0) cmd = sdsCat(cmd, " ");
    cmd = sdsCatLen(cmd, (char*)listNodeValue(listIndex(rcmdList, j)), strlen((char*)listNodeValue(listIndex(rcmdList, j))));
    cmd = sdsCat(cmd, " ");
  }

  sds ret = run(execList);
  listAddNodeTail(execList, "\r\n");
  listAddNodeTail(execList, ret);
  
  for (unsigned int j = 0; j < listLength(execList); j++) {
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
  for (unsigned int j = 0; j < listLength(rcmdList); j++) {
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
  for (unsigned int j = 0; j < listLength(rcmdList); j++) {
    cmd = sdsCatLen(cmd, (char*)listNodeValue(listIndex(rcmdList, j)), strlen((char*)listNodeValue(listIndex(rcmdList, j))));
    cmd = sdsCat(cmd, " ");
  }

  for (unsigned int j = 0; j < listLength(execList); j++) {
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
  for (int i = 2; i < argc; i++) {
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
    /*    int status;
    char buf[BUF_SIZE];
    int nread = 0;
    buf[nread] = '\0';
    nread = read(fds[0], buf, BUF_SIZE);
    while (nread == BUF_SIZE || !waitpid(pid, &status, WNOHANG)) {
      sleep(1);
      printf("%s", buf);
      retChar = sdsCat(retChar, buf);
      nread = read(fds[0], buf, BUF_SIZE);
    }
    buf[nread] = '\0';
    printf("%s\n", buf);
    retChar = sdsCat(retChar, buf);
    */
  }
  return retChar;
}

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
