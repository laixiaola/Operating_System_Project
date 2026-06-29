#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LFU.h"

static unsigned long hashKey(const char *key, int size)
{
    unsigned long hash = 5381;
    while (*key) {
        hash = ((hash << 5) + hash) + (*key);
        key++;
    }
    return hash % size;
}

/* 哈希表插入 */
static void mapInsert(LFU *lfu, LFUNode *node)
{
    unsigned long index = hashKey(node->key, lfu->mapSize);
    LFUHashNode *h = malloc(sizeof(LFUHashNode));
    h->key = strdup(node->key);
    h->node = node;
    h->next = lfu->map[index];
    lfu->map[index] = h;
}

/* 哈希表查找 */
static LFUNode *mapGet(LFU *lfu, const char *key)
{
    unsigned long index = hashKey(key, lfu->mapSize);
    LFUHashNode *p = lfu->map[index];
    while (p) {
        if (strcmp(p->key, key) == 0)
            return p->node;
        p = p->next;
    }
    return NULL;
}

/* 哈希表删除 */
static void mapDelete(LFU *lfu, const char *key)
{
    unsigned long index = hashKey(key, lfu->mapSize);
    LFUHashNode *p = lfu->map[index];
    LFUHashNode *pre = NULL;
    while (p) {
        if (strcmp(p->key, key) == 0) {
            if (pre)
                pre->next = p->next;
            else
                lfu->map[index] = p->next;
            free(p->key);
            free(p);
            return;
        }
        pre = p;
        p = p->next;
    }
}

/* 从频率桶中移除节点 */
static void removeFromBucket(FreqBucket *bucket, LFUNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        bucket->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        bucket->tail = node->prev;
}

/* 从频率链表中移除空桶 */
static void removeEmptyBucket(FreqBucket *bucket)
{
    if (bucket->prev)
        bucket->prev->next = bucket->next;
    else {
        // bucket是头节点，需要向上层传递
    }

    if (bucket->next)
        bucket->next->prev = bucket->prev;

    free(bucket);
}

/* 创建新的频率桶 */
static FreqBucket *createBucket(int freq)
{
    FreqBucket *bucket = malloc(sizeof(FreqBucket));
    bucket->freq = freq;
    bucket->head = NULL;
    bucket->tail = NULL;
    bucket->next = NULL;
    bucket->prev = NULL;
    return bucket;
}

/* 查找或创建频率桶 */
static FreqBucket *findOrCreateBucket(LFU *lfu, int freq)
{
    FreqBucket *p = lfu->freqHead;
    FreqBucket *prev = NULL;

    while (p && p->freq < freq) {
        prev = p;
        p = p->next;
    }

    if (p && p->freq == freq)
        return p;

    // 创建新桶并插入
    FreqBucket *newBucket = createBucket(freq);
    if (prev) {
        newBucket->next = prev->next;
        newBucket->prev = prev;
        if (prev->next)
            prev->next->prev = newBucket;
        prev->next = newBucket;
    } else {
        newBucket->next = lfu->freqHead;
        if (lfu->freqHead)
            lfu->freqHead->prev = newBucket;
        lfu->freqHead = newBucket;
    }

    return newBucket;
}

/* 从桶中移除节点并更新最小频率 */
static void removeNode(LFU *lfu, LFUNode *node)
{
    // 找到节点所在的桶
    FreqBucket *bucket = findOrCreateBucket(lfu, node->freq);
    removeFromBucket(bucket, node);

    // 如果桶为空且是当前最小频率，更新minFreq
    if (bucket->head == NULL) {
        if (lfu->minFreq == bucket->freq) {
            // 如果该桶是最小频率桶，找到下一个非空桶
            FreqBucket *nextBucket = bucket->next;
            while (nextBucket && nextBucket->head == NULL)
                nextBucket = nextBucket->next;

            if (nextBucket)
                lfu->minFreq = nextBucket->freq;
            else
                lfu->minFreq = 0;  // 没有更多节点
        }

        // 从频率链表中移除空桶
        if (bucket->prev)
            bucket->prev->next = bucket->next;
        else
            lfu->freqHead = bucket->next;

        if (bucket->next)
            bucket->next->prev = bucket->prev;

        free(bucket);
    }
}

/* 插入节点到频率桶 */
static void insertToBucket(FreqBucket *bucket, LFUNode *node)
{
    node->prev = NULL;
    node->next = bucket->head;

    if (bucket->head)
        bucket->head->prev = node;

    bucket->head = node;

    if (bucket->tail == NULL)
        bucket->tail = node;
}

/* 增加节点频率 */
static void increaseFreq(LFU *lfu, LFUNode *node)
{
    // 从当前频率桶移除
    FreqBucket *oldBucket = findOrCreateBucket(lfu, node->freq);
    removeFromBucket(oldBucket, node);

    // 如果旧桶为空且是当前最小频率，需要更新minFreq
    if (oldBucket->head == NULL) {
        if (lfu->minFreq == oldBucket->freq) {
            // 如果该桶是最小频率桶，找到下一个非空桶
            FreqBucket *nextBucket = oldBucket->next;
            while (nextBucket && nextBucket->head == NULL)
                nextBucket = nextBucket->next;

            if (nextBucket)
                lfu->minFreq = nextBucket->freq;
            else
                lfu->minFreq = 0;
        }

        // 从频率链表中移除空桶
        if (oldBucket->prev)
            oldBucket->prev->next = oldBucket->next;
        else
            lfu->freqHead = oldBucket->next;

        if (oldBucket->next)
            oldBucket->next->prev = oldBucket->prev;

        free(oldBucket);
    }

    // 增加频率
    node->freq++;

    // 插入到新的频率桶
    FreqBucket *newBucket = findOrCreateBucket(lfu, node->freq);
    insertToBucket(newBucket, node);
}

/* 插入头部（用于新节点，频率为1） */
static void insertHead(LFU *lfu, LFUNode *node)
{
    node->freq = 1;
    FreqBucket *bucket = findOrCreateBucket(lfu, 1);
    insertToBucket(bucket, node);

    // 更新最小频率
    if (lfu->minFreq == 0 || lfu->minFreq > 1)
        lfu->minFreq = 1;
}

/* LFU访问函数 */
static void LFU_Access(void *ptr, const char *key)
{
    LFU *lfu = (LFU *)ptr;
    LFUNode *node = mapGet(lfu, key);
    if (node) {
        printf("[LFU] Hit page: %s (freq=%d)\n", key, node->freq);
        increaseFreq(lfu, node);
    }
}

/* LFU插入函数 */
static char *LFU_Insert(void *ptr, const char *key)
{
    LFU *lfu = (LFU *)ptr;
    LFUNode *old = mapGet(lfu, key);

    // 键已存在，增加频率
    if (old) {
        increaseFreq(lfu, old);
        return NULL;
    }

    // 创建新节点
    LFUNode *node = malloc(sizeof(LFUNode));
    node->key = strdup(key);
    node->freq = 0;
    node->prev = NULL;
    node->next = NULL;

    insertHead(lfu, node);
    printf("[LFU] Insert page: %s\n", key);
    mapInsert(lfu, node);
    lfu->size++;

    // 淘汰节点
    if (lfu->size > lfu->capacity) {
        // 找到最小频率桶的尾部节点
        FreqBucket *minBucket = lfu->freqHead;
        while (minBucket && minBucket->freq != lfu->minFreq)
            minBucket = minBucket->next;

        if (minBucket && minBucket->tail) {
            LFUNode *victim = minBucket->tail;
            char *victimKey = strdup(victim->key);

            printf("[LFU] Cache full, replace page: %s (freq=%d)\n", 
                   victim->key, victim->freq);

            // 从桶中移除
            removeNode(lfu, victim);
            mapDelete(lfu, victim->key);

            free(victim->key);
            free(victim);
            lfu->size--;

            return victimKey;
        }
    }

    return NULL;
}

/* LFU删除函数 */
static void LFU_Remove(void *ptr, const char *key)
{
    LFU *lfu = (LFU *)ptr;
    LFUNode *node = mapGet(lfu, key);

    if (node == NULL)
        return;

    removeNode(lfu, node);
    mapDelete(lfu, key);

    free(node->key);
    free(node);

    lfu->size--;
}

/* LFU销毁函数 */
static void LFU_Destroy(void *ptr)
{
    LFU *lfu = (LFU *)ptr;

    // 释放所有LFU节点
    FreqBucket *bucket = lfu->freqHead;
    while (bucket) {
        FreqBucket *nextBucket = bucket->next;
        LFUNode *p = bucket->head;
        while (p) {
            LFUNode *next = p->next;
            free(p->key);
            free(p);
            p = next;
        }
        free(bucket);
        bucket = nextBucket;
    }

    // 释放所有哈希桶
    for (int i = 0; i < lfu->mapSize; i++) {
        LFUHashNode *h = lfu->map[i];
        while (h) {
            LFUHashNode *next = h->next;
            free(h->key);
            free(h);
            h = next;
        }
    }

    free(lfu->map);
    free(lfu);
}

/* 创建LFU策略 */
ReplacementPolicy *LFU_CreatePolicy(int capacity)
{
    LFU *lfu = malloc(sizeof(LFU));

    lfu->freqHead = NULL;
    lfu->size = 0;
    lfu->capacity = capacity;
    lfu->mapSize = 1024;
    lfu->minFreq = 0;

    lfu->map = calloc(lfu->mapSize, sizeof(LFUHashNode *));

    ReplacementPolicy *policy = malloc(sizeof(ReplacementPolicy));

    policy->policy = lfu;
    policy->insert = LFU_Insert;
    policy->access = LFU_Access;
    policy->remove = LFU_Remove;
    policy->destroy = LFU_Destroy;

    return policy;
}