#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "dict.h"

/*
static void _dictPanic(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  fprintf(stderr, "\nDICT LIBRARY PANIC: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n\n");
  va_end(ap);
}
*/

static int _dictExpandIfNeeded(dict *ht);
static unsigned int _dictNextPower(unsigned int size);
//static int _dictKeyIndex(dict *ht, const void *key);

int _dictInit(dict *ht, dictType *type);
int _dictReset(dict *ht);

/* Thomas Wang's 32 bit Mix Function */
unsigned int dictIntHashFunction(unsigned int key)
{
  key += ~(key << 15);
  key ^=  (key >> 10);
  key +=  (key << 3);
  key ^=  (key >> 6);
  key += ~(key << 11);
  key ^=  (key >> 16);
  return key;
}

unsigned int dictGenHashFunction(const unsigned char *buf, int len)
{
  unsigned int hash = 5381;

  while (len--)
    hash = ((hash << 5) + hash) + (*buf++); /* hash * 33 + c */
  return hash;
}

dict *dictCreate(dictType *type)
{
  dict *ht;
  ht = malloc(sizeof(*ht));
  if (ht == NULL)
    fprintf(stderr, "malloc() error.\n");

  _dictInit(ht, type);
  return ht;
}

int _dictInit(dict *ht, dictType *type)
{
  _dictReset(ht);
  ht->type = type;
  return DICT_OK;
}

int _dictReset(dict *ht)
{
  ht->table = NULL;
  ht->size = 0;
  ht->sizemask = 0;
  ht->used = 0;
  return DICT_OK;
}

static int _dictExpand(dict *ht, unsigned int size)
{
  dict newht;
  dictEntry * de, *deNext;

  if (ht->used > size)
    return DICT_ERR;

  unsigned int realsize = _dictNextPower(size);

  _dictInit(&newht, ht->type);
  newht.table = malloc(realsize * sizeof(dictEntry *));
  newht.size = realsize;
  newht.used = ht->used;
  newht.sizemask = realsize - 1;

  memset(newht.table, 0, realsize * sizeof(dictEntry *));

  for (unsigned int i = 0; i < ht->size && ht->used > 0; i++) {
    de = ht->table[i];
    if (de == NULL) continue;

    while (de) {
      unsigned int h;
      deNext = de->next;
      h = dictHashKey(&newht, de->key) & newht.sizemask;
      de->next = newht.table[h];
      newht.table[h] = de;
      ht->used--;
      de = deNext;
    }
  }
  assert(ht->used == 0);
  free(ht->table);
  ht->table = NULL;
  *ht = newht;
  return DICT_OK;
}

static unsigned int _dictNextPower(unsigned int size)
{
  unsigned int i = 1;
  while (i <= size)
    i *= 2;
  return i;
}

static int _dictExpandIfNeeded(dict *ht)
{
  if (ht->size == 0)
    _dictExpand(ht, DICT_INITIAL_SIZE);
  if (ht->size == ht->used)
    return _dictExpand(ht, ht->size);
  return DICT_OK;
}

int dictAdd(dict *ht, void *key, void *val)
{
  _dictExpandIfNeeded(ht);

  dictEntry *de = malloc(sizeof(dictEntry));
  dictSetHashKey(ht, de, key);
  dictSetHashVal(ht, de, val);

  unsigned int idx;
  idx = dictHashKey(ht, de->key) & ht->sizemask;
  de->next = ht->table[idx];
  ht->table[idx] = de;
  ht->used++;
  return DICT_OK;
}

dictEntry *dictFind(dict *ht, void *key)
{
  unsigned int idx;
  dictEntry *de;

  if (ht->size == 0) return NULL;
  idx = dictHashKey(ht, key) & ht->sizemask;
  de = ht->table[idx];
  while (de) {
    if (dictCompareHashKeys(ht, de->key, key)) 
      break;
    de = de->next;
  }
  return de;
}

int dictReplace(dict *ht, void *key, void *val)
{
  dictEntry *de;

  if (dictAdd(ht, key, val) == DICT_OK)
    return DICT_OK;

  de = dictFind(ht, key);
  dictFreeHashVal(ht, de);
  dictSetHashVal(ht, de, val);
  return DICT_OK;
}

int dictDelete(dict *ht, const void *key)
{
  dictEntry *de, *dePrev;
  unsigned int idx;

  idx = dictHashKey(ht, key) & ht->sizemask;
  de = ht->table[idx];
  dePrev = NULL;
  while (de) {
    if (dictCompareHashKeys(ht, de->key, key)) {
      if (dePrev)
	dePrev->next = de->next;
      else
	ht->table[idx] = de->next;
      dictFreeHashKey(ht, de);
      dictFreeHashVal(ht, de);
      free(de);
      ht->used--;
      return DICT_OK;
    }
    dePrev = de;
    de = de->next;
  }
  return DICT_ERR;
}

void dictRelease(dict *ht)
{
  unsigned int idx;
  dictEntry *de, *deNext;
  for (idx = 0; idx < ht->size; idx++) {
    de = ht->table[idx];
    while (de) {
      deNext = de->next;
      dictFreeHashKey(ht, de);
      dictFreeHashVal(ht, de);
      free(de);
      ht->used--;
      de = deNext;
    }
  }
  free(ht);
  _dictReset(ht);
}

int dictResize(dict *ht)
{
  int minimal = ht->used;

  if (minimal < DICT_INITIAL_SIZE)
    minimal = DICT_INITIAL_SIZE;
  return _dictExpand(ht, minimal);
}

void dictEmpty(dict *ht)
{
  for (unsigned int i = 0; i < ht->size && ht->used > 0; i++) {
    dictEntry *de, *nextDe;

    if ((de = ht->table[i]) == NULL)
      continue;
    while (de) {
      nextDe = de->next;
      dictFreeHashKey(ht, de);
      dictFreeHashVal(ht, de);
      free(de);
      ht->used--;
      de = nextDe;
    }
  }
}

dictIterator *dictGetIterator(dict *ht)
{
  dictIterator *iter = malloc(sizeof(*iter));
  
  iter->ht = ht;
  iter->index = -1;
  iter->entry = NULL;
  iter->nextEntry = NULL;
  return iter;
}

dictEntry *dictNext(dictIterator *iter)
{
  while (1) {
    if (iter->entry == NULL) {
      iter->index++;
      if (iter->index >=
	  (signed)iter->ht->size) break;
      iter->entry = iter->ht->table[iter->index];
    } else {
      iter->entry = iter->nextEntry;
    }
    if (iter->entry) {
      /* We need to save the 'next' here, the iterator user
       * may delete the entry we are returning. */
      iter->nextEntry = iter->entry->next;
      return iter->entry;
    }
  }
  return NULL;
}

void dictReleaseIterator(dictIterator *iter)
{
  free(iter);
}
