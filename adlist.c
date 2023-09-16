#include <stdlib.h>
#include "adlist.h"

list* listCreate(void)
{
  list *list;
  if ((list = malloc(sizeof(*list))) == NULL)
    return NULL;
  list->head = list->tail = NULL;
  list->len = 0;
  list->dup = NULL;
  list->free = NULL;
  list->match = NULL;
  return list;
}

void listRelease(list *list)
{
  listNode *node = list->head, *prev;

  if (node == NULL)
    free(list);
  
  while (node) {
    prev = node->prev;
    if (list->free) list->free(node->value);
    free(node);
    node = prev;
  }
  return;
}

list* listAddNodeHead(list *list, void *value)
{
  listNode *node;

  if ((node = malloc(sizeof(*node))) == NULL)
    return NULL;

  if (list->len == 0) {
    list->head = list->tail = node;
    node->prev = node->next = NULL;
  } else {
    node->prev = NULL;
    node->next = list->head;
    list->head->prev = node;
    list->head = node;
  }
  node->value = value;
  list->len++;
  return list;
}

list *listAddNodeTail(list *list, void *value)
{
  listNode *node;

  if ((node = malloc(sizeof(*node))) == NULL)
    return NULL;

  node->value = value;
  if (list->len == 0) {
    list->head = list->tail = node;
    node->prev = node->next = NULL;
  } else {
//    node->next = NULL;
    node->prev = list->tail;
    node->next = NULL;
    list->tail->next = node;
    list->tail = node;
  }
  list->len++;
  
  return list;
}

void listDelNode(list *list, listNode *node)
{
  if (node->prev)
    node->prev->next = node->next;
  else
    list->head = node->next;

  if (node->next)
    node->next->prev = node->prev;
  else
    list->tail = node->prev;
  if (list->free)
    list->free(node->value);
  free(node);
  list->len--;

  return;
}

listIter *listGetIterator(list *list, int direction)
{
  listIter *iter;

  iter = malloc(sizeof(*iter));
  if (direction == AL_START_HEAD) {
    iter->next = list->head;
    iter->prev = NULL;
  } else if (direction == AL_START_TAIL) {
    iter->next = list->tail;
    iter->prev = NULL;
  }
  iter->direction = direction;
  
  return iter;
}

listNode *listNextElement(listIter *iter)
{
  listNode *node = iter->next;

  if (node != NULL) {
    if (iter->direction == AL_START_HEAD)
      iter->next = node->next;
    else
      iter->next = node->prev;
  }
  return node;
}

void listReleaseIterator(listIter *iter)
{
  free(iter);
}

listNode *listSearchKey(list *list, void *key)
{
  listNode *node = list->head;

  while (node) {
    if (list->match) {
      if (list->match(node->value, key))
	return node;
    } else {
      if (key == node->value)
	return node;
    }
    node = node->next;
  }
  return NULL;
}

listNode *listIndex(list *list, int index)
{
  // unsigned和int求和不安全
  /*  listNode *node = list->head;

  index = (index < 0) ? (list->len + index) : index;
  while (index--) {
    node = node->next;
  }
  */
  listNode *node;
  if (index < 0) {
    index = (-index) - 1;
    node = list->tail;
    while (index-- && node) node = node->prev;
  } else {
    node = list->head;
    while (index-- && node) node = node->next;
  }
  return node;
}
