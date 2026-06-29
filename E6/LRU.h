#ifndef LRU_H
#define LRU_H

#include "Policy.h"

/*LRU链表节点*/
typedef struct LRUNode
{
    char *key;
    struct LRUNode *prev;
    struct LRUNode *next;
}LRUNode;

/*LRU内部Hash节点 */
typedef struct LRUHashNode
{
    char *key;
    LRUNode *node;
    struct LRUHashNode *next;
}LRUHashNode;

/* LRU结构*/
typedef struct
{
    LRUNode *head;
    LRUNode *tail;
    LRUHashNode **map;
    int mapSize;
    int size;
    int capacity;
}LRU;

/** 创建LRU策略*/
ReplacementPolicy *LRU_CreatePolicy(int capacity);


#endif