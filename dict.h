#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

typedef struct dictEntry {
  void * key;
  void * val;
  struct dictEntry * next;
} dictEntry;

typedef struct dictType {
  unsigned int (*hashFunction)(const void *key);
  void *(*keyDup)(const void *key);
  void *(*valDup)(const void *val);
  int (*keyCompare)(const void *key1, const void *key2);
  void (*keyDestructor)(void *key);
  void (*valDestructor)(void *val);
} dictType;

typedef struct dict {
  dictEntry **table;
  dictType *type;
  unsigned int size;
  unsigned int sizemask;
  unsigned int used;
} dict;

typedef struct dictIterator {
    dict *ht;
    int index;
    dictEntry *entry, *nextEntry;
} dictIterator;

#define DICT_INITIAL_SIZE 16

#define dictSetHashKey(ht, entry, _key_) do { \
  if ((ht)->type->keyDup)			 \
    (entry)->key = (ht)->type->keyDup(_key_);	 \
  else						 \
    entry->key = (_key_);			 \
  } while(0)

#define dictSetHashVal(ht, entry, _val_) do { \
  if ((ht)->type->valDup)		      \
    (entry)->val = (ht)->type->keyDup(_val_);  \
  else					      \
    (entry)->val = (_val_);		      \
  } while (0)

#define dictFreeHashKey(ht, entry)		\
  if ((ht)->type->keyDestructor)		\
    (ht)->type->keyDestructor((entry)->key)

#define dictFreeHashVal(ht, entry)		\
  if ((ht)->type->valDestructor)		\
    (ht)->type->valDestructor((entry)->val)

#define dictCompareHashKeys(ht, key1, key2)	\
  (((ht)->type->keyCompare) ?			\
   (ht)->type->keyCompare(key1, key2) :		\
   (key1) == (key2))

#define dictHashKey(ht, key)			\
  (ht)->type->hashFunction(key)

#define dictGetEntryKey(he) ((he)->key)
#define dictGetEntryVal(he) ((he)->val)
#define dictGetHashTableSize(ht) ((ht)->size)
#define dictGetHashTableUsed(ht) ((ht)->used)

dict *dictCreate(dictType* type);
unsigned int dictGenHashFunction(const unsigned char *buf, int len);
unsigned int dictIntHashFunction(unsigned int key);
int dictAdd(dict *ht, void *key, void *val);
int dictReplace(dict *ht, void *key, void *val);
dictEntry *dictFind(dict *ht, void *key);
int dictDelete(dict *ht, const void *key);
void dictRelease(dict *ht);
int dictResize(dict *ht);
void dictEmpty(dict *ht);
dictIterator *dictGetIterator(dict *ht);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);

#endif	/* __DICT_H */
