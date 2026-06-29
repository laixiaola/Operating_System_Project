#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LRU.h"

static unsigned long hashKey(const char *key, int size)
{
    unsigned long hash = 5381;

    while (*key) {
        hash = ((hash << 5) + hash) + (*key);
        key++;
    }

    return hash % size;
}

static void mapInsert(LRU *lru, LRUNode *node)
{
    unsigned long index = hashKey(node->key, lru->mapSize);

    LRUHashNode *h = malloc(sizeof(LRUHashNode));

    h->key = strdup(node->key);
    h->node = node;
    h->next = lru->map[index];   // 头插法

    lru->map[index] = h;
}


static LRUNode *mapGet(LRU *lru, const char *key)
{
    unsigned long index = hashKey(key, lru->mapSize);

    LRUHashNode *p = lru->map[index];

    while (p) {
        if (strcmp(p->key, key) == 0)
            return p->node;
        p = p->next;
    }

    return NULL;
}

static void mapDelete(LRU *lru, const char *key)
{
    unsigned long index = hashKey(key, lru->mapSize);

    LRUHashNode *p = lru->map[index];
    LRUHashNode *pre = NULL;

    while (p) {
        if (strcmp(p->key, key) == 0) {
            // 从链表中移除
            if (pre)
                pre->next = p->next;
            else
                lru->map[index] = p->next;

            free(p->key);
            free(p);
            return;
        }

        pre = p;
        p = p->next;
    }
}


static void removeNode(LRU *lru, LRUNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        lru->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        lru->tail = node->prev;
}

static void insertHead(LRU *lru, LRUNode *node)
{
    node->prev = NULL;
    node->next = lru->head;

    if (lru->head)
        lru->head->prev = node;

    lru->head = node;

    if (lru->tail == NULL)
        lru->tail = node;
}

static void moveToHead(LRU *lru, LRUNode *node)
{
    if(node==lru->head) return;
    removeNode(lru, node);
    insertHead(lru, node);
}

static void LRU_Access(void *ptr, const char *key)
{
    LRU *lru = (LRU *)ptr;
    LRUNode *node =mapGet(lru, key);
    if (node)
    {
        printf("[LRU] Hit page: %s\n",key);
        moveToHead(lru, node);
    }
}

static char *LRU_Insert(void *ptr, const char *key)
{
    LRU *lru = (LRU*)ptr;
    LRUNode *old = mapGet(lru, key);

    // 键已存在，更新访问顺序
    if (old) {
        moveToHead(lru, old);
        return NULL;
    }

    // 创建新节点
    LRUNode *node = malloc(sizeof(LRUNode));
    node->key = strdup(key);

    insertHead(lru, node);
    printf("[LRU] Insert page: %s\n",key);
    mapInsert(lru, node);
    lru->size++;

    // 淘汰尾部节点
    if (lru->size > lru->capacity)
    {

        LRUNode *victim =lru->tail;
        char *key =strdup(victim->key);
        printf("[LRU] Cache full, replace page: %s\n",victim->key);
        removeNode(lru, victim);
        mapDelete(lru, victim->key);
        free(victim->key);
        free(victim);
        lru->size--;

        return key;
    }

    return NULL;
}

static void LRU_Remove(void *ptr, const char *key)
{
    LRU *lru = (LRU*)ptr;
    LRUNode *node = mapGet(lru, key);

    if (node == NULL)
        return;

    removeNode(lru, node);
    mapDelete(lru, key);

    free(node->key);
    free(node);

    lru->size--;
}

static void LRU_Destroy(void *ptr)
{
    LRU *lru = (LRU*)ptr;

    // 释放所有LRU节点
    LRUNode *p = lru->head;
    while (p) {
        LRUNode *next = p->next;
        free(p->key);
        free(p);
        p = next;
    }

    // 释放所有哈希桶
    for (int i = 0; i < lru->mapSize; i++) {
        LRUHashNode *h = lru->map[i];
        while (h) {
            LRUHashNode *next = h->next;
            free(h->key);
            free(h);
            h = next;
        }
    }

    free(lru->map);
    free(lru);
}

ReplacementPolicy *LRU_CreatePolicy(int capacity)
{
    LRU *lru = malloc(sizeof(LRU));

    lru->head = NULL;
    lru->tail = NULL;
    lru->size = 0;
    lru->capacity = capacity;
    lru->mapSize = 1024;

    lru->map = calloc(lru->mapSize, sizeof(LRUHashNode*));

    ReplacementPolicy *policy = malloc(sizeof(ReplacementPolicy));

    policy->policy = lru;
    policy->insert = LRU_Insert;
    policy->access = LRU_Access;
    policy->remove = LRU_Remove;
    policy->destroy = LRU_Destroy;

    return policy;
}