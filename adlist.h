#ifndef __ADLIST_H
#define __ADLIST_H

typedef struct listNode {
  struct listNode *prev;
  struct listNode *next;
  void *value;
} listNode;

typedef struct list {
  listNode *head;
  listNode *tail;
  void *(*dup)(void *ptr);
  void (*free)(void *ptr);
  int (*match)(void *ptr, void *key);
  unsigned int len;
} list;

typedef struct listIter {
  listNode *next;
  listNode *prev;
  int direction;
} listIter;

#define listLength(l) ((l)->len)
#define listFirst(l) ((l)->head)
#define listLast(l) ((l)->tail)
#define listPrevNode(n) ((n)->prev)
#define listNextNode(n) ((n)->next)
#define listNodeValue(n) ((n)->value)

#define listSetDupMethod(l, m) ((l)->dup = (m))
#define listSetFreeMethod(l, m) ((l)->free = (m))
#define listSetMatchMethod(l, m) ((l)->match = (m))

list* listCreate(void);
void listRelease(list *list);
list* listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
void listDelNode(list *list, listNode *node);
listIter *listGetIterator(list *list, int direction);
listNode *listNextElement(listIter *iter);
void listReleaseIterator(listIter *iter);
listNode *listSearchKey(list *list, void *key);
listNode *listIndex(list *list, int index);

#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif
