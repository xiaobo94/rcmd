#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <time.h>

#include "sds.h"
#include "adlist.h"
#include "dict.h"
#include "ae.h"
#include "aUnixDomain.h"

#define RCMD_VERSION "0.0.1"
#define RCMD_QUERYBUF_LEN 1024
#define RCMD_LOADBUF_LEN 1024

#define RCMD_OK 0
#define RCMD_ERR 1

#define RCMD_BULK 1
#define RCMD_INLINE 2

#define RCMD_DEFAULT_DBNUM 8

#define RCMD_DEBUG 0
#define RCMD_NOTICE 1
#define RCMD_WARNING 2

#define RCMD_HT_MINFILL 10
#define RCMD_HT_MINSLOTS 16384

#define RCMD_QUERYBUF_LEN 1024
#define RCMD_MAX_ARGS 16

#define RCMD_STRING 0
#define RCMD_LIST 1

#define RCMD_STRING 0
#define RCMD_LIST 1
#define RCMD_SELECTDB 254
#define RCMD_EOF 255

#define RCMD_NOTUSED(V) ((void) V)

/* ======================== Data Types ======================== */

typedef struct rcmdObject {
  int type;
  void *ptr;
  int idx;
  int refcount;
} robj;

typedef struct rcmdClient {
  int fd;
  dict *dict;
  int dictid;
  sds querybuf;
  robj *argv[RCMD_MAX_ARGS];
  int argc;
  int bulklen;			/* bulk read len. -1 if not in bulk read mode */
  list *reply;
  int sentlen;
  robj *soft;
  robj *cmds;
  robj *contents;
  list *cmdList;
  
  //  int flags;
} rcmdClient;

struct saveParam {
  int changes;
  time_t seconds;
};

struct rcmdServer {
  list *clients;
  dict **dict;
  int fd;
  aeEventLoop *el;
  int cronloops;
  int verbosity;
  int dbnum;
  char *logfile;
  int daemonize;
  char *dbfilename;
  int bgSaveInProgress;
  long long dirty;
  time_t lastSave;
  struct saveParam *saveParams;
  int saveParamsLen;
  list *cmdList;
  
  //  int stat_numconnections;
};

typedef void rcmdProc(rcmdClient *c);
struct rcmdCommand {
  char *name;
  rcmdProc *proc;;
  int flags;
};

struct sharedObjectsStruct {
  robj *ok, *err;
} shared;

/* ======================== Prototypes ======================== */

static void oom(const char *msg);
static robj *createObject(int type, void *ptr);
static void decrRefCount(void *o);
static int robjMatch(void *ptr, void *key);
static void incrRefCount(robj *o);
static void freeStringObject(robj *o);
static void freeListObject(robj *o);
static void addReply(rcmdClient *c, robj *obj);
static void addReplySds(rcmdClient *c, sds s);
static int loadDb(char *filename);
static int saveDbBackground(char *filename);
static robj *createStringObject(char *ptr, size_t len);
static robj *createListObject();
static void appendServerSaveParams(time_t seconds, int changes);
static void ResetServerSaveParams();
static void saddCommand(rcmdClient *c);
static void sgetCommand(rcmdClient *c);
static void saveCommand(rcmdClient *c);
static void sdelCommand(rcmdClient *c);
static void allCmdCommand(rcmdClient *c);

static char *soft(robj *r);

/* ======================== Globals ======================== */

static struct rcmdServer server;
static struct rcmdCommand cmdTable[] = {
  {"sadd", saddCommand, RCMD_BULK},
  {"sget", sgetCommand, RCMD_BULK},
  {"save", saveCommand, RCMD_BULK},
  {"sdel", sdelCommand, RCMD_BULK},
  {"allcmd", allCmdCommand, RCMD_BULK},
  {NULL, NULL, 0}
};

/* ======================== Utility functions ======================== */

static char* soft(robj *r)
{
  char *p, *q;
  sds s;
  
  p = strstr((char*)r->ptr, "\r\n");
  q = strchr((char*)r->ptr, ' ');

  if (q == NULL || q >= p)
    s = sdsNewLen((char*)r->ptr, p - (char*)r->ptr);
  else
    s = sdsNewLen((char*)r->ptr, q - (char*)r->ptr);
  return s;
}

void rcmdLog(int level, const char *fmt, ...)
{
  va_list ap;
  FILE *fp;

  fp = (server.logfile == NULL) ? stdout : fopen(server.logfile, "a");
  if (!fp) return;

  va_start(ap, fmt);
  if (level >= server.verbosity) {
    char *c = ".-*";
    fprintf(fp, "%c ", c[level]);
    vfprintf(fp, fmt, ap);
    fprintf(fp, "\n");
    fflush(fp);
  }
  va_end(ap);

  if (server.logfile)
    fclose(fp);
}

static void appendServerSaveParams(time_t seconds, int changes)
{
  server.saveParams = realloc(server.saveParams,sizeof(struct saveParam)*(server.saveParamsLen+1));
  if (server.saveParams == NULL) oom("appendServerSaveParams");
  server.saveParams[server.saveParamsLen].seconds = seconds;
  server.saveParams[server.saveParamsLen].changes = changes;
  server.saveParamsLen++;
}

static void ResetServerSaveParams()
{
  free(server.saveParams);
  server.saveParams = NULL;
  server.saveParamsLen = 0;
}

/* ======================== hash table type implementation ======================== */

static unsigned int dictSdsHash(const void *key)
{
  const robj *o = key;
  return dictGenHashFunction(o->ptr, sdsLen((sds)o->ptr));
}

static int dictSdsKeyCompare(const void *key1, const void *key2)
{
  int l1, l2, ret;

  l1 = sdsLen((sds) key1);
  l2 = sdsLen((sds) key2);
  
  if (l1 > l2) {
    ret = memcmp(key1, key2, l2);
    if (ret < 0)
      return ret;
    else
      return 1;
  } else if (l1 < l2) {
    ret = memcmp(key1, key2, l1);
    if (ret > 0)
      return ret;
    else
      return -1;
  }
  return memcmp(key1, key2, l1);
}

static void dictRedisObjectDestructor(void *val)
{
  decrRefCount(val);
}

static dictType hashDictType = {
  dictSdsHash,			/* hash function */
  NULL,
  NULL,
  dictSdsKeyCompare,
  dictRedisObjectDestructor,
  dictRedisObjectDestructor
};

/* ======================== Random utility functions ======================== */

static void oom(const char *msg) {
  fprintf(stderr, "%s: Out of memory\n", msg);
  fflush(stderr);
  sleep(1);
  abort();
}

/* ======================== rcmd server stuff ======================== */

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData)
{
  int size, used, loops = server.cronloops++;
  RCMD_NOTUSED(eventLoop);
  RCMD_NOTUSED(id);
  RCMD_NOTUSED(clientData);

  for (int j = 0; j < server.dbnum; j++) {
    size = dictGetHashTableSize(server.dict[j]);
    used = dictGetHashTableUsed(server.dict[j]);
    if (!(loops % 5) && used > 0) {
      rcmdLog(RCMD_DEBUG, "DB %d: %d keys in %d slots HT.", j, used, size);
    }
    if (size && used && size > RCMD_HT_MINSLOTS && (used * 100 / size < RCMD_HT_MINFILL)) {
      rcmdLog(RCMD_NOTICE, "The hash table %d is too sparse, resize it...", j);
      dictResize(server.dict[j]);
      rcmdLog(RCMD_NOTICE, "Hash table %d resized.", j);
    }
  }

  if (server.bgSaveInProgress) {
    int statloc;

    if (wait4(-1, &statloc, WNOHANG, NULL)) {
      int exitcode = WEXITSTATUS(statloc);
      if (exitcode == 0) {
	rcmdLog(RCMD_NOTICE, "Background saving terminated with success");
	server.dirty = 0;
	server.lastSave = time(NULL);
      } else {
	rcmdLog(RCMD_WARNING, "Background saving error");
      }
      server.bgSaveInProgress = 0;
    }
  } else {
    time_t now = time(NULL);
    for (int j = 0; j < server.saveParamsLen; j++) {
      struct saveParam *sp = server.saveParams + j;

      if (server.dirty >= sp->changes && now - server.lastSave > sp->seconds) {
	rcmdLog(RCMD_NOTICE, "%d changes in %d seconds. Saving...", sp->changes, sp->seconds);
	saveDbBackground(server.dbfilename);
	break;
      }
    }
  }

  return 1000;
}

static void createSharedObjects(void) {
  shared.ok = createObject(RCMD_STRING, sdsNew("OK\r\n"));
  shared.err = createObject(RCMD_STRING, sdsNew("ERR\r\n"));
}

static void initServerConfig()
{
  server.dbnum = RCMD_DEFAULT_DBNUM;
  server.verbosity = RCMD_DEBUG;
  server.logfile = NULL; /* NULL = log on standard output */
  server.daemonize = 0;
  server.dbfilename = "rcmd.db";
  ResetServerSaveParams();

  appendServerSaveParams(60*60,1);  /* save after 1 hour and 1 change */
  appendServerSaveParams(300,100);  /* save after 5 minutes and 10 changes */
  appendServerSaveParams(60,10000); /* save after 1 minute and 50 changes */
}

static void initServer() {
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  server.clients = listCreate();
  createSharedObjects();
  server.el = aeCreateEventLoop();
  server.dict = malloc(sizeof(dict*) * server.dbnum);
  server.fd = aUnixDomainServer();

  for (int i = 0; i < server.dbnum; i++) {
    server.dict[i] = dictCreate(&hashDictType);
    if (!server.dict[i])
      oom("dictCreate");
  }
  server.cmdList = listCreate();
  server.cronloops = 0;
  server.bgSaveInProgress = 0;
  aeCreateTimeEvent(server.el, 1000, serverCron, NULL, NULL);
}

/*
static void emptyDb()
{
  for (int j = 0; j < server.dbnum; j++)
    dictEmpty(server.dict[j]);
}
*/

//static void loadServerConfig(char *filename);

static void freeClientArgv(rcmdClient *c) 
{
  for (int j = 0; j < c->argc; j++)
    decrRefCount(c->argv[j]);
  c->argc = 0;
}

static void freeClient(rcmdClient *c)
{
  listNode *ln;

  aeDeleteFileEvent(server.el, c->fd, AE_READABLE);
  aeDeleteFileEvent(server.el, c->fd, AE_WRITABLE);
  sdsFree(c->querybuf);
  listRelease(c->reply);
  freeClientArgv(c);
  close(c->fd);
  ln = listSearchKey(server.clients, c);
  assert(ln != NULL);
  listDelNode(server.clients, ln);
  free(c);
}

static void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask)
{
  rcmdClient *c = privdata;
  int totwritten = 0, objlen;
  robj *o;
  sds retchr = sdsEmpty();
  RCMD_NOTUSED(el);
  RCMD_NOTUSED(mask);

  while (listLength(c->reply)) {
    o = listNodeValue(listFirst(c->reply));
    objlen = sdsLen(o->ptr);

    if (objlen == 0) {
      listDelNode(c->reply, listFirst(c->reply));
      continue;
    }

    if (o->idx >= 0) {
      retchr = sdsCatPrintf(retchr, ">>> Index %d\n", o->idx);
    }

    retchr = sdsCatLen(retchr, o->ptr, objlen);
    //    nwritten = write(fd, o->ptr + c->sentlen, objlen - c->sentlen);
    //    if (nwritten <= 0)
    //      break;
    //    c->sentlen += nwritten;
    totwritten += objlen;
    listDelNode(c->reply, listFirst(c->reply));
    //    if (c->sentlen == objlen) {
    //      listDelNode(c->reply, listFirst(c->reply));
    //      c->sentlen = 0;
    //    }
  }
  sds retnum = sdsCatPrintf(sdsEmpty(), "%d\r\n", (int)sdsLen(retchr));
  if ((write(fd, retnum, sdsLen(retnum)) == -1) || (write(fd, retchr, sdsLen(retchr)) == -1)) {
    if (errno == EAGAIN) {
      totwritten = 0;
    } else {
      rcmdLog(RCMD_DEBUG, "Error writing to client: %s", strerror(errno));
      freeClient(c);
      return; 
    }
  }
  sdsFree(retnum);
  sdsFree(retchr);
  if (listLength(c->reply) == 0) {
    c->sentlen = 0;
    aeDeleteFileEvent(server.el, c->fd, AE_WRITABLE);
  }
}

static struct rcmdCommand *lookupCommand(char *name)
{
  int j = 0;
  while (cmdTable[j].name != NULL) {
    if (!strcmp(name, cmdTable[j].name))
      return &cmdTable[j];
    j++;
  }
  return NULL;
}

static void resetClient(rcmdClient *c)
{
  freeClientArgv(c);
  c->bulklen = -1;
}
  
static int processCommand(rcmdClient *c)
{
  struct rcmdCommand *cmd;
//  long long dirty;

  if (!strcmp(c->argv[0]->ptr, "quit")) {
    freeClient(c);
    return 0;
  }
  cmd = lookupCommand(c->argv[0]->ptr);
  if (!cmd) {
    addReplySds(c, sdsNew("-ERR unknown command\r\n"));
    resetClient(c);
    return 1;
  } else if (cmd->flags & RCMD_BULK && c->bulklen == -1) {
    int bulklen = atoi(c->argv[c->argc - 1]->ptr);

    decrRefCount(c->argv[c->argc - 1]);
    if (bulklen < 0 || bulklen > 1024 * 1024 * 1024) {
      c->argc--;
      addReplySds(c, sdsNew("-ERR invalid bulk write count\r\n"));
      resetClient(c);
      return 1;
    }
    c->argc--;
    c->bulklen = bulklen;
//    c->contents = createStringObject(c->querybuf, sdsLen(c->querybuf));

    if ((signed) sdsLen(c->querybuf) >= c->bulklen) {
      c->argv[c->argc] = createStringObject(c->querybuf, c->bulklen - 2);
      c->argc++;
//      c->querybuf = sdsRange(c->querybuf, c->bulklen, -1);
    } else {
      return 1;
    }
  }

  c->contents = createStringObject(c->querybuf, sdsLen(c->querybuf));
  c->querybuf = sdsRange(c->querybuf, c->bulklen, -1);
  cmd->proc(c);

//  if (c->flags & RCMD_CLOSE) {
//    freeClient(c);
//    return 0;
//  }
  resetClient(c);
  return 1;
}

static void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask)
{
  rcmdClient *c = (rcmdClient *) privdata;
  char buf[RCMD_QUERYBUF_LEN];
  int nread;
  RCMD_NOTUSED(el);
  RCMD_NOTUSED(mask);

  nread = read(fd, buf, RCMD_QUERYBUF_LEN);
  if (nread == -1) {
    if (errno == EAGAIN) {	/* 资源暂时不可用，非阻塞操作中使用 */
      nread = 0;
    } else {
      rcmdLog(RCMD_DEBUG, "Reading from client: %s", strerror(errno));
      freeClient(c);
      return;
    }
  } else if (nread == 0) {
    rcmdLog(RCMD_DEBUG, "Client closed connection");
    freeClient(c);
    return;
  }
  if (nread) {
    c->querybuf = sdsCatLen(c->querybuf, buf, nread);
  } else {
    return;
  }

 again:
  if (c->bulklen == -1) {
    char *p = strstr(c->querybuf, "\r\n");
    size_t querylen;
    if (p) {
      sds query, *argv;
      int argc, j;

      query = c->querybuf;
      c->querybuf = sdsEmpty();
      querylen = 2 + (p - query);
      if (sdsLen(query) > querylen) {
	c->querybuf = sdsCatLen(c->querybuf, query + querylen, sdsLen(query) - querylen);
      }
      *p = '\0';
      *(p+1) = '\0';
      sdsUpdateLen(query);

      if (sdsLen(query) == 0) {
	sdsFree(query);
	return;
      }
      argv = sdsSplitLen(query, sdsLen(query), " ", 1, &argc);
      sdsFree(query);
      if (argv == NULL)
	oom("sdsSplitLen");
      for (j = 0; j < argc && j < RCMD_MAX_ARGS; j++) {
	if (sdsLen(argv[j])) {
	  c->argv[c->argc] = createObject(RCMD_STRING, argv[j]);;
	  c->argc++;
	}
      }
      free(argv);
      p = strstr(c->querybuf, "\r\n");
      sds tmp = sdsNewLen(c->querybuf, p - c->querybuf);
      p = strchr(tmp, ' ');
      if (p != NULL)
	c->soft = createStringObject(tmp, p - tmp);
      else
	c->soft = createStringObject(tmp, sdsLen(tmp));
      sdsFree(tmp);
      //      p = strstr(c->querybuf, "\r\n");
      //      c->cmds = createStringObject(c->querybuf, p - c->querybuf);
      //      len = strstr(p + 2, "\r\n") - p - 2;
      //      c->contents = createStringObject(c->querybuf, sdsLen(c->querybuf));

      //      c->soft->ptr = malloc(p - c->querybuf + 1);
      //      memcpy(c->soft->ptr, c->querybuf, p - c->querybuf + 1);
      //      c->soft[p - c->querybuf] = '\0';
      if (processCommand(c) && sdsLen(c->querybuf)) goto again;
      return;
    } else if (sdsLen(c->querybuf) >= 1024) {
      rcmdLog(RCMD_DEBUG, "Client protocol error");
      freeClient(c);
      return;
    }
  } else {
    int qbl = sdsLen(c->querybuf);

    if (c->bulklen <= qbl) {
//      c->argv[c->argc] = createStringObject(c->querybuf, c->bulklen - 2);
//      c->argc++;
//      c->querybuf = sdsRange(c->querybuf, c->bulklen, -1);
      processCommand(c);
      return; 
    }
  }
}

static int selectDb(rcmdClient *c, int id)
{
  if (id < 0 || id >= server.dbnum)
    return RCMD_ERR;
  c->dict = server.dict[id];
  c->dictid = id;
  return RCMD_OK;
}

static rcmdClient *createClient(int fd)
{
  rcmdClient *c;
  c = malloc(sizeof(*c));

  if (!c) return NULL;
  selectDb(c, 0);
  c->cmdList = server.cmdList;
  c->fd = fd;
  c->querybuf = sdsEmpty();
  c->bulklen = -1;
  c->sentlen = 0;
  c->cmds = NULL;
  c->argc = 0;
  if ((c->reply = listCreate()) == NULL)
    oom("listCreate");
  listSetFreeMethod(c->reply, decrRefCount);
  listSetFreeMethod(c->cmdList, decrRefCount);
  listSetMatchMethod(c->cmdList, robjMatch);
  if (aeCreateFileEvent(server.el, c->fd, AE_READABLE, readQueryFromClient, c, NULL) == AE_ERR) {
    freeClient(c);
    return NULL;
  }
  if (!listAddNodeTail(server.clients, c))
    oom("listAddNodeTail");
  return c;
}

static void addReply(rcmdClient *c, robj *obj)
{
  if (listLength(c->reply) == 0 && aeCreateFileEvent(server.el, c->fd, AE_WRITABLE, sendReplyToClient, c, NULL) == AE_ERR)
    return;
  if (!listAddNodeTail(c->reply, obj))
    oom("listAddNodeTail");
  incrRefCount(obj);
}

static void addReplySds(rcmdClient *c, sds s)
{
  robj *o = createObject(RCMD_STRING, s);
  addReply(c, o);
  decrRefCount(o);
}

static void acceptHandler(aeEventLoop *el, int fd, void *privdata, int mask)
{
  int cfd = aUnixDomainAccept(fd);
  RCMD_NOTUSED(el);
  RCMD_NOTUSED(privdata);
  RCMD_NOTUSED(mask);

  rcmdLog(RCMD_DEBUG, "Client %d has connected", cfd);
  if (createClient(cfd) == NULL) {
    rcmdLog(RCMD_WARNING, "Error allocating resoures for the client");
    close(cfd);
    return;
  }
  //  server.stat_numconnections++;
}

/* ======================= rcmd objects implementation ======================= */

static robj *createObject(int type, void *ptr)
{
  robj *o = malloc(sizeof(*o));
  if (!o)
    oom("createObject");
  o->type = type;
  o->ptr = ptr;
  o->idx = -1;
  o->refcount = 1;
  return o;
}

static robj *createStringObject(char *ptr, size_t len)
{
  return createObject(RCMD_STRING, sdsNewLen(ptr, len));
}

static robj *createListObject()
{
  list * l = listCreate();

  if (!l) oom("listCreate");
  listSetFreeMethod(l,decrRefCount);
  return createObject(RCMD_LIST, l);
}

static void freeStringObject(robj *o)
{
  sdsFree(o->ptr);
}

static void freeListObject(robj *o)
{
  listRelease(o->ptr);
}

static void incrRefCount(robj *o)
{
  o->refcount++;
}

static void decrRefCount(void *obj)
{
  robj *o = obj;
  if (--(o->refcount) == 0) {
    switch(o->type) {
    case RCMD_STRING: freeStringObject(o); break;
    case RCMD_LIST: freeListObject(o); break;
    default: assert(0 != 0); break;
    }
  }
}

static int robjMatch(void *ptr, void *key)
{
  robj *p = ptr, *k = key;
  return sdsCmp(p->ptr, k->ptr);
}

/* ============================ DB saving/loading ============================ */

static int saveDb(char *filename)
{
  dictIterator *di = NULL;
  dictEntry *de;
  uint32_t len;
  uint8_t type;
  FILE *fp;
  char tmpfile[256];

  snprintf(tmpfile, 256, "temp-%d.%ld.db", (int)time(NULL), (long int)random());
  fp = fopen(tmpfile, "w");
  if (!fp) {
    rcmdLog(RCMD_WARNING, "Failed saving the DB: %s", strerror(errno));
    return RCMD_ERR;
  }
  if (fwrite("RCMD0000", 8, 1, fp) == 0)
    goto werr;
  for (int j = 0; j < server.dbnum; j++) {
    dict *d = server.dict[j];
    if (dictGetHashTableUsed(d) == 0)
      continue;
    di = dictGetIterator(d);
    if (!di) {
      fclose(fp);
      return RCMD_ERR;
    }

    type = RCMD_SELECTDB;
    len = htonl(j);
    if (fwrite(&type, 1, 1, fp) == 0) goto werr;
    if (fwrite(&len, 4, 1, fp) == 0) goto werr;

    while ((de = dictNext(di)) != NULL) {
      robj *key = dictGetEntryKey(de);
      robj *o = dictGetEntryVal(de);

      type = o->type;
      len = htonl(sdsLen(key->ptr));
      if (fwrite(&type,1,1,fp) == 0) goto werr;
      if (fwrite(&len,4,1,fp) == 0) goto werr;
      if (fwrite(key->ptr,sdsLen(key->ptr),1,fp) == 0) goto werr;
      if (type == RCMD_LIST) {
	list *list = o->ptr;
	listNode *ln = list->head;

	len = htonl(listLength(list));
	if (fwrite(&len, 4, 1, fp) == 0) goto werr;
	while (ln) {
	  robj *eleobj = listNodeValue(ln);
	  len = htonl(sdsLen(eleobj->ptr));
	  if (fwrite(&len, 4, 1, fp) == 0) goto werr;
	  if (sdsLen(eleobj->ptr) && fwrite(eleobj->ptr, sdsLen(eleobj->ptr), 1, fp) == 0)
	    goto werr;
	  ln = ln->next;
	}
      } else if (type == RCMD_STRING) {
	sds sval = o->ptr;
	len = htonl(sdsLen(sval));
	if (fwrite(&len, 4, 1, fp) == 0) goto werr;
	if (sdsLen(sval) && fwrite(sval, sdsLen(sval), 1, fp) == 0) goto werr;
      } else {
	assert(0 != 0);
      }
    }
    dictReleaseIterator(di);
  }
  type = RCMD_EOF;
  if (fwrite(&type, 1, 1, fp) == 0) goto werr;
  fflush(fp);
  fsync(fileno(fp));
  fclose(fp);

  if (rename(tmpfile, filename) == -1) {
    rcmdLog(RCMD_WARNING, "Error moving temp DB file on the final destionation: %s", strerror(errno));
    unlink(tmpfile);
    return RCMD_ERR;
  }

  rcmdLog(RCMD_NOTICE,"DB saved on disk");
  server.dirty = 0;
  return RCMD_OK;

 werr:
  fclose(fp);
  unlink(tmpfile);
  rcmdLog(RCMD_WARNING,"Write error saving DB on disk: %s", strerror(errno));
  if (di) dictReleaseIterator(di);
  return RCMD_ERR;
}

static int saveDbBackground(char *filename)
{
  pid_t pid;

  if (server.bgSaveInProgress) return RCMD_ERR;
  if ((pid = fork()) == 0) {
    close(server.fd);
    if (saveDb(filename) == RCMD_OK)
      exit(0);
    else
      exit(1);
  } else {
    rcmdLog(RCMD_NOTICE, "Background saving started by pid %d", pid);
    server.bgSaveInProgress = 1;
    return RCMD_OK;
  }
  return RCMD_OK;
}

static int loadDb(char *filename)
{
  FILE *fp;
  char buf[RCMD_LOADBUF_LEN];
  char vbuf[RCMD_LOADBUF_LEN];
  char *key = NULL, *val = NULL;
  uint32_t klen, vlen, dbid;
  uint8_t type;
  int retval;
  dict *d = server.dict[0];

  fp = fopen(filename, "r");
  if (!fp) return RCMD_ERR;
  if (fread(buf, 8, 1, fp) == 0) goto eoferr;
  if (memcmp(buf, "RCMD0000", 8) != 0) {
    fclose(fp);
    rcmdLog(RCMD_WARNING, "Wrong signature trying to load DB from file");
    return RCMD_ERR;
  }
  while (1) {
    robj *o, *k;

    /* read type */
    if (fread(&type, 1, 1, fp) == 0) goto eoferr;
    if (type == RCMD_EOF) break;

    if (type == RCMD_SELECTDB) {
      if (fread(&dbid, 4, 1, fp) == 0) goto eoferr;
      dbid = ntohl(dbid);
      if (dbid >= (unsigned)server.dbnum) {
	rcmdLog(RCMD_WARNING, "FATEL: Data file was created with a Redis server compiled to handle more than %d databases. Exiting\n", server.dbnum);
	exit(1);
      }
      d = server.dict[dbid];
      continue;
    }

    if (fread(&klen, 4, 1, fp) == 0) goto eoferr;
    klen = ntohl(klen);
    if (klen <= RCMD_LOADBUF_LEN) {
      key = buf;
    } else {
      key = malloc(klen);
      if (!key) oom("Loading DB from file");
    }
    if (fread(key, klen, 1, fp) == 0) goto eoferr;

    if (type == RCMD_STRING) {
      if (fread(&vlen, 4, 1, fp) == 0) goto eoferr;
      vlen = ntohl(vlen);
      if (vlen <= RCMD_LOADBUF_LEN) {
	val = vbuf;
      } else {
	val = malloc(vlen);
	if (!val) oom("Loading DB from file");
      }
      if (vlen && fread(val, vlen, 1, fp) == 0) goto eoferr;
      o = createObject(RCMD_STRING, sdsNewLen(val, vlen));
    } else if (type == RCMD_LIST) {
      uint32_t listlen;
      if (fread(&listlen, 4, 1, fp) == 0) goto eoferr;
      listlen = ntohl(listlen);
      o = createListObject();
      listSetMatchMethod((list*)o->ptr, robjMatch);
      while (listlen--) {
	robj *ele;

	if (fread(&vlen, 4, 1, fp) == 0) goto eoferr;
	vlen = ntohl(vlen);
	if (vlen <= RCMD_LOADBUF_LEN) {
	  val = vbuf;
	} else {
	  val = malloc(vlen);
	  if (!val) oom("Loading DB from file");
	}
	if (vlen && fread(val, vlen, 1, fp) == 0) goto eoferr;
	ele = createObject(RCMD_STRING, sdsNewLen(val, vlen));
	if (!listAddNodeTail((list*)o->ptr, ele))
	  oom("listAddNodeTail");
	if (val != vbuf)
	  free(val);
	val = NULL;
      }
    } else {
      assert(0 != 0);
    }

    k = createStringObject(key, klen);
    retval = dictAdd(d, k, o);
    if (listSearchKey(server.cmdList, k) == NULL)
      listAddNodeTail(server.cmdList, k);
    if (retval == DICT_ERR) {
      rcmdLog(RCMD_WARNING, "Loading DB, duplicated key found! Unrecoverable error, exiting now.");
      exit(1);
    }

    if (key != buf) free(key);
    if (val != vbuf) free(val);
    key = val = NULL;
  }
  fclose(fp);
  return RCMD_OK;

 eoferr:
  if (key != buf) free(key);
  if (val != vbuf) free(val);
  rcmdLog(RCMD_WARNING,"Short read loading DB. Unrecoverable error, exiting now.");
  exit(1);
  return RCMD_ERR;
}

/* ============================ Commands ============================ */

/* ============================ Strings ============================ */

static void saddCommand(rcmdClient *c)
{
  dictEntry *de = NULL;
  listNode *ln = NULL;
  if (c->dict->size == 0 || (de = dictFind(c->dict, c->soft)) == NULL) {
    c->cmds = createListObject();
    listSetMatchMethod((list*)c->cmds->ptr, robjMatch);
    dictAdd(c->dict, c->soft, c->cmds);
  } else if (de) {
    c->cmds = dictGetEntryVal(de);
  }

  if ((ln = listSearchKey(c->cmdList, c->soft)) == NULL)
    listAddNodeTail(c->cmdList, c->soft);

  listAddNodeTail(c->cmds->ptr, c->contents);
  server.dirty++;
  addReply(c, shared.ok);
}

static void sgetCommand(rcmdClient *c)
{
  dictEntry *de;
  robj *contents;
  sds s;
  int idx = 0;

  de = dictFind(c->dict, c->soft);
  if (de == NULL) {
    addReply(c, shared.err);
  } else {
    robj *o = dictGetEntryVal(de);

    if (o->type != RCMD_LIST) {
      addReply(c, shared.err);
    } else {
      //      addReplySds(c, sdsCatPrintf(sdsEmpty(), "%d\r\n", (int)sdsLen(o->ptr)));
      //      addReply(c, o);
      listNode *ln;
      listIter *iter = listGetIterator(o->ptr, AL_START_HEAD);
      while ((ln = listNextElement(iter))) {
	contents = listNodeValue(ln);
	s = soft(contents);
	if (strncmp(c->soft->ptr, s, (strlen(s) < strlen((char*)c->soft->ptr) ? strlen(s) : strlen((char*)c->soft->ptr)))) {
	  sdsFree(s);
	  continue;
	}
	sdsFree(s);
	contents->idx = idx++;
//	addReplySds(c, sdsCatPrintf(sdsEmpty(), "%d\r\n", (int)sdsLen(contents->ptr)));
	addReply(c, contents);
      }
    }
  }
}

static void saveCommand(rcmdClient *c)
{
  if (saveDb(server.dbfilename) == RCMD_OK)
    addReply(c, shared.ok);
  else
    addReply(c, shared.err);
}

static void sdelCommand(rcmdClient *c)
{
  dictEntry *de = NULL;
  listNode *ln = NULL;
  listIter *iter;
  if (c->dict->size == 0 || (de = dictFind(c->dict, c->soft)) == NULL) {
    return addReply(c, shared.err);
  } else if (de) {
    c->cmds = dictGetEntryVal(de);
    if (listSearchKey((list*)c->cmds->ptr, c->soft) == NULL)
      return addReply(c, shared.err);
  }

  //  listDelNode(c->cmds->ptr, listIndex(c->cmds->ptr, atoi((char*)c->argv[c->argc - 1])));

  /*  if (listLength((list*)c->cmds->ptr) == 1) {
    listDelNode(c->cmdList, listSearchKey(c->cmdList, c->soft));
    if (dictDelete(c->dict, c->soft) == DICT_ERR)
      return addReply(c, shared.err);
      }*/
  int idx = 0;
  char *p = strchr((char*)c->argv[c->argc - 1]->ptr, ' ');
  if (p) {
    idx = atoi(p + 1);
    iter = listGetIterator(c->cmds->ptr, AL_START_HEAD);
  } else {
    iter = listGetIterator(c->cmds->ptr, AL_START_TAIL);
  }
  while ((idx >= 0) && (ln = listNextElement(iter)) != NULL) {
    if (!((list*)(c->cmds->ptr))->match(ln->value, c->soft)) {
      idx--;
    }
  }
  listDelNode(c->cmds->ptr, ln);

  if (listSearchKey(c->cmds->ptr, c->soft) == NULL) {
//    listRelease(c->cmds->ptr);
//    c->cmds->ptr = NULL;
    if (listLength((list*)c->cmds->ptr) == 0)
      dictDelete(c->dict, c->soft);
    listDelNode(c->cmdList, listSearchKey(c->cmdList, c->soft));
  }
    
  return addReply(c, shared.ok);
}

static void allCmdCommand(rcmdClient *c)
{
  listNode *ln;
  robj *o;
  sds ret = sdsEmpty();
  int idx = 0;
  listIter *iter = listGetIterator(c->cmdList, AL_START_HEAD);

  while ((ln = listNextElement(iter))) {
    o = listNodeValue(ln);
    ret = sdsCatPrintf(ret, "%d: ", idx++);
    ret = sdsCat(ret, o->ptr);
    ret = sdsCat(ret, "\n");
  }
  
  o = createObject(RCMD_STRING, ret);
  return addReply(c, o);
}

/*
static void saddCommand(rcmdClient *c)
{
  int retval;

  retval = dictAdd(c->dict, c->argv[1], c->argv[2]);
  incrRefCount(c->argv[1]);
  incrRefCount(c->argv[2]);

  addReply(c, shared.ok);
}
*/

int main(int argc, char *argv[])
{
  initServerConfig();
  if (argc == 2) {
    /* 读取配置文件 */
    ResetServerSaveParams();
    //loadServerConfig(argv[1]);
    RCMD_NOTUSED(argv);
  } else if (argc > 2) {
    fprintf(stderr, "Usage: ./rcmd [/path/to/rcmd.conf]\n");
  }
  initServer();

  if (loadDb(server.dbfilename) == RCMD_OK)
    rcmdLog(RCMD_NOTICE, "DB loaded from disk");
  
  if (aeCreateFileEvent(server.el, server.fd, AE_READABLE, acceptHandler, NULL, NULL) == AE_ERR)
    oom("create file event");
  rcmdLog(RCMD_NOTICE, "The server is now ready to accept connections");
  aeMain(server.el);
  aeDeleteEventLoop(server.el);
  return 0;
}
