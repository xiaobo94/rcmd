#ifndef __AE_H__
#define __AE_H__

struct aeEventLoop;

typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);

typedef struct aeFileEvent {
  int fd;
  int mask; 			/* one of AE_(READABLE|WRITABLE|EXCEPTION) */
  aeFileProc *fileProc;
  aeEventFinalizerProc *finalizerProc;
  void *clientData;
  struct aeFileEvent *next;
} aeFileEvent;

typedef struct aeTimeEvent {
  long long id;
  long when_sec;
  long when_ms;
  aeTimeProc *timeProc;
  aeEventFinalizerProc *finalizerProc;
  void *clientData;
  struct aeTimeEvent *next;
} aeTimeEvent;

typedef struct aeEventLoop {
  long long timeEventNextId;
  aeFileEvent *fileEventHead;
  aeTimeEvent *timeEventHead;
  int stop;
} aeEventLoop;

#define AE_OK 0
#define AE_ERR -1

#define AE_READABLE 1
#define AE_WRITABLE 2
#define AE_EXCEPTION 4

#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4

#define AE_NOMORE -1

aeEventLoop *aeCreateEventLoop(void);
void aeDeleteEventLoop(aeEventLoop *eventLoop);
void aeStop(aeEventLoop *eventLoop);
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask, aeFileProc *proc, void *clientData, aeEventFinalizerProc *finalizerProc);
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fe, int mask);
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds, aeTimeProc *proc, void *clientData, aeEventFinalizerProc *finalizerProc);
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
int aeWait(int fd, int mask, long long milliseconds);
void aeMain(aeEventLoop *eventLoop);

#endif 
