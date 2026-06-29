#ifndef CACHE_H
#define CACHE_H

#include "Hash.h"
#include "Policy.h"
#include <pthread.h>

typedef struct
{
    hashtable *table;          // 底层哈希表
    ReplacementPolicy *policy; // 置换策略接口
    pthread_mutex_t mutex;
} Cache;

Cache *Cache_Create(int hashSize, ReplacementPolicy *policy);
void Cache_Destroy(Cache *cache);
content *Cache_Get(Cache *cache, char *key);
int Cache_Put(Cache *cache, char *key, content *cont);
int Cache_Delete(Cache *cache, char *key);

#endif