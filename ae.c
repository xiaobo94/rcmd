#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "ae.h"

aeEventLoop *aeCreateEventLoop(void)
{
  aeEventLoop * eventLoop;

  if ((eventLoop = malloc(sizeof(*eventLoop))) == NULL) {
    fputs("malloc() error", stderr);
    exit(1);
  }

  eventLoop->timeEventNextId = 0;
  eventLoop->fileEventHead = NULL;
  eventLoop->timeEventHead = NULL;
  eventLoop->stop = 0;

  return eventLoop;
}

void aeDeleteEventLoop(aeEventLoop *eventLoop)
{
  free(eventLoop);
}

void aeStop(aeEventLoop *eventLoop)
{
  eventLoop->stop = 1;
}

int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask, aeFileProc *proc, void *clientData, aeEventFinalizerProc *finalizerProc)
{
  aeFileEvent *fe;

  if ((fe = malloc(sizeof(*fe))) == NULL) {
    fputs("malloc() error\n", stderr);
    return AE_ERR;
  }
  fe->fd = fd;
  fe->mask = mask;
  fe->fileProc = proc;
  fe->finalizerProc = finalizerProc;
  fe->clientData = clientData;
  fe->next = eventLoop->fileEventHead;
  eventLoop->fileEventHead = fe;
  return AE_OK;
}

void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
  aeFileEvent *fe, *prev = NULL;

  fe = eventLoop->fileEventHead;
  while (fe) {
    if (fe->fd == fd && fe->mask == mask) {
      if (prev == NULL)
	eventLoop->fileEventHead = fe->next;
      else
	prev->next = fe->next;
      if (fe->finalizerProc)
	fe->finalizerProc(eventLoop, fe->clientData);
      free(fe);
      return;
    }
    prev = fe;
    fe = fe->next;
  }
}

static void aeGetTime(long *seconds, long *milliseconds)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  *seconds = tv.tv_sec;
  *milliseconds = tv.tv_usec / 1000;
}

static void aeAddMilliSecondsToNow(long long milliseconds, long *sec, long *ms)
{
  long cur_sec, cur_ms, when_sec, when_ms;

  aeGetTime(&cur_sec, &cur_ms);
  when_sec = cur_sec + milliseconds / 1000;
  when_ms = cur_ms + milliseconds % 1000;
  if (when_ms >= 1000) {
    when_sec++;
    when_ms -= 1000;
  }
  *sec = when_sec;
  *ms = when_ms;
}

long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds, aeTimeProc *proc, void *clientData, aeEventFinalizerProc *finalizerProc)
{
  long long id = eventLoop->timeEventNextId++;
  aeTimeEvent *te;

  if ((te = malloc(sizeof(*te))) == NULL) {
    fputs("malloc() error\n", stderr);
    return AE_ERR;
  }
  te->id = id;
  aeAddMilliSecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
  te->timeProc = proc;
  te->finalizerProc = finalizerProc;
  te->clientData = clientData;
  te->next = eventLoop->timeEventHead;
  eventLoop->timeEventHead = te;
  return id;
}

int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
  aeTimeEvent *te, *prev = NULL;

  te = eventLoop->timeEventHead;
  while (te) {
    if (te->id == id) {
      if (prev == NULL)
	eventLoop->timeEventHead = te->next;
      else
	prev->next = te->next;
      if (te->finalizerProc)
	te->finalizerProc(eventLoop, te->clientData);
      free(te);
      return AE_OK;
    }
    prev = te;
    te = te->next;
  }
  return AE_ERR;
}

static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
  aeTimeEvent *te = eventLoop->timeEventHead;
  aeTimeEvent *nearest = NULL;

  while (te) {
    if (!nearest || te->when_sec < nearest->when_sec || (te->when_sec == nearest->when_sec && te->when_ms < nearest->when_ms))
      nearest = te;
    te = te->next;
  }
  return nearest;
}

int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
  int maxfd = 0, numfd = 0, processed = 0;
  fd_set rfds, wfds, efds;
  aeFileEvent *fe = eventLoop->fileEventHead;
  aeTimeEvent *te;
  long long maxId;

  if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS))
    return 0;

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);

  if (flags & AE_FILE_EVENTS) {
    while (fe != NULL) {
      if (fe->mask & AE_READABLE) FD_SET(fe->fd, &rfds);
      if (fe->mask & AE_WRITABLE) FD_SET(fe->fd, &wfds);
      if (fe->mask & AE_EXCEPTION) FD_SET(fe->fd, &efds);
      if (maxfd < fe->fd) maxfd = fe->fd;
      numfd++;
      fe = fe->next;
    }
  }

  if (numfd || ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
    int retval;
    aeTimeEvent *shortest = NULL;
    struct timeval tv, *tvp;

    if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
      shortest = aeSearchNearestTimer(eventLoop);
    if (shortest) {
      long now_sec, now_ms;

      aeGetTime(&now_sec, &now_ms);
      tvp = &tv;
      tvp->tv_sec = shortest->when_sec - now_sec;
      if (shortest->when_ms < now_ms) {
	tvp->tv_usec = ((shortest->when_ms + 1000) - now_ms) * 1000;
	tvp->tv_sec--;
      } else {
	tvp->tv_usec = (shortest->when_ms - now_ms) * 1000;
      }
    } else {
      if (flags & AE_DONT_WAIT) {
	tv.tv_sec = tv.tv_usec = 0;
	tvp = &tv;
      } else {
	tvp = NULL;
      }
    }

    retval = select(maxfd + 1, &rfds, &wfds, &efds, tvp);
    if (retval > 0) {
      fe = eventLoop->fileEventHead;
      while (fe != NULL) {
	int fd = (int) fe->fd;

	if ((fe->mask & AE_READABLE && FD_ISSET(fd, &rfds)) ||
	    (fe->mask & AE_WRITABLE && FD_ISSET(fd, &wfds)) ||
	    (fe->mask & AE_EXCEPTION && FD_ISSET(fd, &efds))) {
	  int mask = 0;

	  if (fe->mask & AE_READABLE && FD_ISSET(fd, &rfds))
	    mask |= AE_READABLE;
	  if (fe->mask & AE_WRITABLE && FD_ISSET(fd, &wfds))
	    mask |= AE_WRITABLE;
	  if (fe->mask & AE_EXCEPTION && FD_ISSET(fd, &efds))
	    mask |= AE_EXCEPTION;
	  fe->fileProc(eventLoop, fe->fd, fe->clientData, mask);
	  processed++;

	  fe = eventLoop->fileEventHead;
	  FD_CLR(fd, &rfds);
	  FD_CLR(fd, &wfds);
	  FD_CLR(fd, &efds);
	} else {
	  fe = fe->next;
	}
      }
    }
  }

  if (flags & AE_TIME_EVENTS) {
    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId - 1;
    while (te) {
      long now_sec, now_ms;
      long long id;

      if (te->id > maxId) {
	te = te->next;
	continue;
      }
      aeGetTime(&now_sec, &now_ms);
      if (now_sec > te->when_sec || (now_sec == te->when_sec && now_ms >= te->when_ms)) {
	int retval;

	id = te->id;
	retval = te->timeProc(eventLoop, id, te->clientData);

	if (retval != AE_NOMORE) {
	  aeAddMilliSecondsToNow(retval, &te->when_sec, &te->when_ms);
	} else {
	  aeDeleteTimeEvent(eventLoop, id);
	}
	te = eventLoop->timeEventHead;
      } else {
	te = te->next;
      }
    }
  }
  return processed;
}

int aeWait(int fd, int mask, long long milliseconds)
{
  struct timeval tv;
  fd_set rfds, wfds, efds;
  int retmask = 0, retval;

  tv.tv_sec = milliseconds / 1000;
  tv.tv_usec = (milliseconds % 1000) * 1000;
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);

  if (mask & AE_READABLE) FD_SET(fd, &rfds);
  if (mask & AE_WRITABLE) FD_SET(fd, &wfds);
  if (mask & AE_EXCEPTION) FD_SET(fd, &efds);

  if ((retval = select(fd + 1, &rfds, &wfds, &efds, &tv)) > 0) {
    if (FD_ISSET(fd, &rfds)) retmask |= AE_READABLE;
    if (FD_ISSET(fd, &wfds)) retmask |= AE_WRITABLE;
    if (FD_ISSET(fd, &efds)) retmask |= AE_EXCEPTION;
    return retmask;
  } else {
    return retval;
  }
}

void aeMain(aeEventLoop *eventLoop)
{
  eventLoop->stop = 0;
  while (!eventLoop->stop)
//    aeProcessEvents(eventLoop, AE_FILE_EVENTS);
    aeProcessEvents(eventLoop, AE_ALL_EVENTS);
}
