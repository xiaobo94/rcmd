#include "sds.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

sds sdsNewLen(const void *init, size_t initLen)
{
  struct sdshdr * sh;

  sh = malloc(sizeof(struct sdshdr) + initLen + 1);
  if (sh == NULL)
    fprintf(stderr, "malloc() error");

  sh->len = initLen;
  if (strlen(init) >= initLen)
    sh->free = 0;
  else
    sh->free = initLen - strlen(init);

  if (initLen) {
    if (init)
      memcpy(sh->buf, init, initLen);
    else
      memset(sh->buf, 0, initLen);
  } 

  sh->buf[initLen] = '\0';
  return (char *)sh->buf;
}

sds sdsNew(const char *init)
{
  size_t initLen = (init == NULL) ? 0 : strlen(init);
  return sdsNewLen(init, initLen);
}

sds sdsEmpty()
{
  return sdsNewLen("", 0);
}

size_t sdsLen(const sds s)
{
  struct sdshdr *sh;
  sh = (void *)(s - sizeof(*sh));

  return sh->len;
}

sds sdsDup(const sds s)
{
  return sdsNewLen(s, sdsLen(s));
}

void sdsFree(sds s)
{
  if (s == NULL)
    return ;
  free(s - sizeof(struct sdshdr));
}

size_t sdsAvail(const sds s)
{
  struct sdshdr *sh = (void *) (s - sizeof(struct sdshdr));
  return sh->free;
}

static sds sdsMakeRoom(sds s, size_t len)
{
  struct sdshdr *sh, *newsh;
  size_t freeLen = sdsAvail(s);

  if (freeLen >= len) return s;

  sh = (void *)(s - sizeof(struct sdshdr));
  size_t useLen = sdsLen(s);
  newsh = realloc(sh, sizeof(struct sdshdr) + (useLen + len) * 2 + 1);
  if (newsh == NULL)
    fprintf(stderr, "realloc() error");
  newsh->len = useLen + len;
  newsh->free = useLen + 2 * len;
  return newsh->buf;
}

sds sdsCatLen(sds s, void *t, size_t len)
{
  struct sdshdr *sh;
  size_t oldLen = sdsLen(s);

  s = sdsMakeRoom(s, len);
  memcpy(s + oldLen, t, len);
  sh = (void *)(s - sizeof(struct sdshdr));
  sh->len = oldLen + len;
  sh->free = sh->free - len;
  s[sh->len] = '\0';
  return s;
}

sds sdsCat(sds s, char * t)
{
  return sdsCatLen(s, t, strlen(t));
}

sds sdsTrimSet(sds s, const char *cTrim)
{
  struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
  char *start, *end, *sp, *ep;
  size_t len;
  
  sp = start = s;
  ep = end = s+sdsLen(s)-1;
  while(sp <= end && strchr(cTrim, *sp)) sp++;
  while(ep > start && strchr(cTrim, *ep)) ep--;
  len = (sp > ep) ? 0 : ((ep-sp)+1);
  if (sh->buf != sp) memmove(sh->buf, sp, len);
  sh->buf[len] = '\0';
  sh->free = sh->free+(sh->len-len);
  sh->len = len;
  return s;
}

/* bug, need modify
sds sdsTrim(sds s, const char *cTrim)
{
  struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
  char *sp, *ep, *cp;
  size_t len = strlen(cTrim);
  
  sp = s;
  ep = s + sdsLen(s) - len;
  cp = cTrim;
  for (int i = 0; i < len; i++) {
    if (*sp == *cp) {
      sp++;
      cp++;
    } else {
      sp = s;
      break;
    }
  }

  cp = cTrim;
  for (int i = 0; i < len; i++) {
    if (*ep == *cp) {
      ep++;
      cp++;
    } else
      break;
  }
  if (ep == s + sdsLen(s))
    ep = s + sdsLen(s) - strlen(cTrim);
  else
    ep = s + sdsLen(s);

  len = (sp > ep) ? 0 : (ep - sp);
  if (sh->buf != sp) memmove(sh->buf, sp, len);
  sh->buf[len] = '\0';
  sh->free = sh->free + (sh->len - len);
  sh->len = len;
  return sh->buf;
}
*/

sds sdsRange(sds s, long start, long end)
{
  struct sdshdr *newsh;
  size_t newLen, len = sdsLen(s);

  if (len == 0) return s;
  if (start < 0) {
    start = start + len;
    if (start < 0)
      start = 0;
  }
  if (end < 0) {
    end = end + len;
    if (end < 0)
      end = 0;
  }

  newLen = (start >= end) ? 0 : (end - start) + 1;
  if (newLen == 0)
    return sdsEmpty();
  else {
    if (start >= (signed)len) start = len;
    if (end >= (signed)len) end = len;
    newLen = (start >= end) ? 0 : (end - start);
  }
  newsh = (void *)(sdsNewLen(s + start, newLen) - sizeof(struct sdshdr));
  return newsh->buf;
}

int sdsCmp(sds s1, sds s2)
{
  int len_s1, len_s2, min_len;
  int cmp;
  
  len_s1 = sdsLen(s1);
  len_s2 = sdsLen(s2);

  min_len = (len_s1 < len_s2) ? len_s1 : len_s2;
  cmp = memcmp(s1, s2, min_len);
  return cmp;
}

sds *sdsSplitLen(char *s, int len, char *sep, int seplen, int *count)
{
  int elements = 0, slots = 5, start = 0, j;

  if (len < 0)
    fprintf(stderr, "The source string's length is less than 0.\n");
  if (seplen < 1)
    fprintf(stderr, "The sep string's length is less than 1.\n");
  
  sds *tokens = malloc(sizeof(sds) * slots);
  if (tokens == NULL)
    fprintf(stderr, "malloc() error");

  for (j = 0; j < (len - (seplen - 1)); j++) {
    if (slots < elements + 2) {
      slots *= 2;
      sds *newSlots = realloc(tokens, sizeof(sds) * slots);
      if (newSlots == NULL)
	goto cleanup;
      tokens = newSlots;
    }

    if ((seplen == 1 && *(s + j) == sep[0]) || (memcmp(s + j, sep, seplen) == 0)) {
      tokens[elements] = sdsNewLen(s + start, j - start);
      if (tokens[elements] == NULL)
	goto cleanup;
      elements++;
      start = j + seplen;
      j = j + seplen - 1;
    }
  }
  tokens[elements] = sdsNewLen(s + start, len - start);
  if (tokens[elements] == NULL)
    goto cleanup;
  elements++;
  *count = elements;
  return tokens;

cleanup:
  {
    int i;
    for (i = 0; i < elements; i++) sdsFree(tokens[i]);
    free(tokens);
    return NULL;
  }
}

void sdsFreeSplitRes(sds *tokens, int count)
{
  if (!tokens) return;
  while (count--)
    sdsFree(tokens[count]);
  free(tokens);
}

void sdsUpdateLen(sds s)
{
  struct sdshdr *sh = (void *)(s - sizeof(struct sdshdr));
  sh->len = strlen(s);
}

int sdsStartsWith(sds s, char *str)
{
  size_t len = strlen(str);
  if (strlen(str) <= len && strncmp(s, str, len) == 0)
    return SDS_TRUE;
  return SDS_FALSE;
}

sds sdsCatPrintf(sds s, const char *fmt, ...)
{
  va_list ap;
  char *buf, *t;
  size_t buflen = 32;

  while (1) {
    buf = malloc(buflen);
    if (buf == NULL) return NULL;

    buf[buflen - 2] = '\0';
    va_start(ap, fmt);
    vsnprintf(buf, buflen, fmt, ap);
    va_end(ap);
    if (buf[buflen -2] != '\0') {
      free(buf);
      buflen *= 2;
      continue;
    }
    break;
  }
  t = sdsCat(s, buf);
  free(buf);
  return t;
}

