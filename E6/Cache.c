#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <stdatomic.h>
#include "Cache.h"

Cache *Cache_Create(int hashSize, ReplacementPolicy *policy)
{
    Cache *cache = malloc(sizeof(Cache));
    pthread_mutex_init(&cache->mutex,NULL);
    cache->table = create_hash(hashSize);
    cache->policy = policy;

    return cache;
}

void Cache_Destroy(Cache *cache)
{
    if(cache == NULL)
        return;

    delete_hash(cache->table);

    if(cache->policy)
    {
        cache->policy->destroy(cache->policy->policy);
        free(cache->policy);
    }

    free(cache);
}

content *Cache_Get(Cache *cache, char *key)
{
    if(cache == NULL || key == NULL) return NULL;
    pthread_mutex_lock(&cache->mutex);
    content *cont = getContentByKey(cache->table, key);

    if(cont != NULL)
    {
        // atomic_fetch_add(&cont->ref,1);
        cache->policy->access(cache->policy->policy, key);
    }
    pthread_mutex_unlock(&cache->mutex);
    return cont;
}

int Cache_Put(Cache *cache, char *key, content *cont)
{
    if(cache == NULL)    return -1;

    pthread_mutex_lock(&cache->mutex);
    /* 插入Hash */
    addItem(cache->table, key, cont);

    /* 通知页面置换算法 */
    char *victim =
        cache->policy->insert(
            cache->policy->policy,
            key);

    if(victim != NULL)
    {
        content *old =getContentByKey(cache->table,victim);
        printf("[Cache] Evict %s\n", victim);
        delItem(cache->table, victim);
        if (old)
        {
            free(old->address);
            free(old);
        }
        free(victim);
    }
    pthread_mutex_unlock(&cache->mutex);
    return 0;
}

int Cache_Delete(Cache *cache, char *key)
{
    if(cache == NULL) return -1;
    pthread_mutex_lock(&cache->mutex);
    cache->policy->remove(cache->policy->policy,key);
    pthread_mutex_unlock(&cache->mutex);
    return delItem(cache->table, key);
}