#ifndef __SDS_H
#define __SDS_H

#include <sys/types.h>

#define SDS_TRUE 1
#define SDS_FALSE 0

typedef char *sds;

struct sdshdr {
    long len;
    long free;
    char buf[0];
};

sds sdsNewLen(const void *init, size_t initlen);
sds sdsNew(const char *init);
sds sdsEmpty();
size_t sdsLen(const sds s);
sds sdsDup(const sds s);
void sdsFree(sds s);
size_t sdsAvail(sds s);
sds sdsCatLen(sds s, void *t, size_t len);
sds sdsCat(sds s, char *t);
//sds sdsTrim(sds s, const char *cset);
sds sdsTrimSet(sds s, const char *cTrim);
sds sdsRange(sds s, long start, long end);
int sdsCmp(sds s1, sds s2);
sds *sdsSplitLen(char *s, int len, char *sep, int seplen, int *count);
void sdsFreeSplitRes(sds *tokens, int count);
void sdsUpdateLen(sds s);
int sdsStartsWith(sds s, char *str);
sds sdsCatPrintf(sds s, const char *fmt, ...);

#endif
