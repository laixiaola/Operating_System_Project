#ifndef LFU_H
#define LFU_H

#include "Policy.h"

/* LFU链表节点 - 按频率分组 */
typedef struct LFUNode
{
    char *key;
    int freq;
    struct LFUNode *prev;
    struct LFUNode *next;
} LFUNode;

/* LFU内部Hash节点 */
typedef struct LFUHashNode
{
    char *key;
    LFUNode *node;
    struct LFUHashNode *next;
} LFUHashNode;

/* 频率桶节点 - 用于组织相同频率的节点 */
typedef struct FreqBucket
{
    int freq;
    LFUNode *head;
    LFUNode *tail;
    struct FreqBucket *next;
    struct FreqBucket *prev;
} FreqBucket;

/* LFU结构 */
typedef struct
{
    LFUHashNode **map;      // 哈希表
    int mapSize;
    FreqBucket *freqHead;   // 频率链表头
    int size;
    int capacity;
    int minFreq;            // 当前最小频率
} LFU;

/* 创建LFU策略 */
ReplacementPolicy *LFU_CreatePolicy(int capacity);

#endif