/*

Copyright 2015 Jonathan Watmough
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
	http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Hash.h"

#ifdef HASHTEST
#include <sys/time.h>
#endif

#ifdef HASHTHREADED
#include <pthread.h>
#include <semaphore.h>
#endif

////////////////////////////////////////////////////////////////////////////////
// STATIC HELPER FUNCTIONS

static inline long int hashString(char * str)
{
	unsigned long hash = 5381;
	int c;

	while (c = *str++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	return hash;
}

// helper for copying string keys and values
static inline char * copystring(char * value)
{
	char * copy = (char *)malloc(strlen(value)+1);
	if(!copy) {
		printf("Unable to allocate string value %s\n",value);
		abort();
	}
	strcpy(copy,value);
	return copy;
}

static inline int isEquealContent(content* cont1, content* cont2){
	if(cont1->length!=cont2->length) return 0;
	if(cont1->address!=cont2->address) return 0;
	return 1;
}


////////////////////////////////////////////////////////////////////////////////
// CREATING A NEW HASH TABLE

// Create hash table
hashtable *create_hash( size_t buckets )
{
	// allocate space
	hashtable *table= (hashtable *)malloc(sizeof(hashtable));
	if(!table) {
		// unable to allocate
		return NULL;
	}
	// locks
#ifdef HASHTHREADED
	//table->lock = 0;
	table->locks = (int *)malloc(buckets * sizeof(int));
	if( !table->locks ) {
		free(table);
		return NULL;
	}
	memset((int *)&table->locks[0],0,buckets*sizeof(int));
#endif
	// setup
	table->bucket = (hashpair **)malloc(buckets*sizeof(void*));
	if( !table->bucket ) {
		free(table);
		return NULL;
	}
	memset(table->bucket,0,buckets*sizeof(void*));
	table->buckets = table->bucketsinitial = buckets;
	HASH_DEBUG("table: %x bucket: %x\n",table,table->bucket);
	return table;
}

void delete_hash(hashtable* table){
	if(table==NULL) return;
	hashpair *next;
	for(int i=0;i<table->buckets;i++){
		hashpair* pair=table->bucket[i];
		while(pair){
			next=pair->next;
			free(pair->key);
			free(pair->cont->address);
			free(pair->cont);
			free(pair);
			pair=next;
		}
	}
	free(table->bucket);
#ifdef HASHTHREADED
	free((void*)table->locks);
#endif
	free(table);
}

////////////////////////////////////////////////////////////////////////////////
// ADDING / DELETING / GETTING BY STRING KEY

// Add str to table - keyed by string
int addItem( hashtable *table, char *key, content* cont )
{
	// compute hash on key
	size_t hash = hashString(key) % table->buckets;
	HASH_DEBUG("adding %s -> %s hash: %ld\n",key,value,hash);

	// add entry
	hashpair *entry = table->bucket[hash];

#ifdef HASHTHREADED
	while(__sync_lock_test_and_set(&table->locks[hash],1)){}
#endif
	
	// already an entry
	HASH_DEBUG("entry: %x\n",entry);
	while(entry!=0)
	{
		HASH_DEBUG("checking entry: %x\n",entry);
		// check for already indexed
		if(0==strcmp(entry->key,key) && isEquealContent(entry->cont,cont)){
#ifdef HASHTHREADED
			__sync_synchronize();
			table->locks[hash] = 0;
#endif
			return 1;
		}
		// check for replacing entry
		if(0==strcmp(entry->key,key) && !isEquealContent(entry->cont,cont))
		{
			// free(entry->cont->address);
			// free(entry->cont);
#ifdef HASHTHREADED
			__sync_synchronize();
			table->locks[hash] = 0;
#endif
			entry->cont = cont;
			return 0;
		}
		// move to next entry
		entry = entry->next;
	}
	
	// create a new entry and add at head of bucket
	HASH_DEBUG("creating new entry\n");
	entry = (hashpair *)malloc(sizeof(hashpair));
	HASH_DEBUG("new entry: %x\n",entry);
	entry->key = copystring(key);
	entry->cont=cont;
	entry->next = table->bucket[hash];
	table->bucket[hash] = entry;
	HASH_DEBUG("added entry\n");

#ifdef HASHTHREADED
	__sync_synchronize();
	table->locks[hash]=0;
#endif

	return 2;
}

// Delete by string
int delItem( hashtable *table, char *key )
{
	// compute hash on key
	size_t hash = hashString(key) % table->buckets;
	HASH_DEBUG("deleting: %s hash: %ld\n",key,hash);

	// add entry
	hashpair *entry = table->bucket[hash];
	hashpair *previous = NULL;
	
	if(entry==0) return 0;

#ifdef HASHTHREADED
	while(__sync_lock_test_and_set(&table->locks[hash],1)){}
#endif

	// found an entry
	HASH_DEBUG("entry: %x\n",entry);
	while(entry!=0)
	{
		HASH_DEBUG("checking entry: %x\n",entry);
		// check for already indexed
		if(0==strcmp(entry->key,key))
		{
			// skip first record, or one in the chain
			if(!previous)
				table->bucket[hash] = entry->next;
			else
				previous->next = entry->next;
			// delete string value if needed
			
			free(entry->key);
			// free(entry->cont->address);
			// free(entry->cont);
			free(entry);
#ifdef HASHTHREADED
			__sync_synchronize();
			table->locks[hash]=0;
#endif
			return 1;
		}
		// move to next entry
		previous = entry;
		entry = entry->next;
	}
#ifdef HASHTHREADED
	__sync_synchronize();
	table->locks[hash]=0;
#endif
	return 0;
}

content* getContentByKey(hashtable *table, char *key)
{
    // 计算哈希桶索引
    size_t hash = hashString(key) % table->buckets;

#ifdef HASHTHREADED
    // 获取该桶的自旋锁（线程安全）
    while (__sync_lock_test_and_set(&table->locks[hash], 1)) {}
#endif

    // 遍历该桶的链表
    hashpair *entry = table->bucket[hash];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            content *c = entry->cont;

#ifdef HASHTHREADED
            // 内存屏障确保数据可见性，释放锁
            __sync_synchronize();
            table->locks[hash] = 0;
#endif
            return c;
        }
        entry = entry->next;
    }

#ifdef HASHTHREADED
    // 未找到，释放锁
    __sync_synchronize();
    table->locks[hash] = 0;
#endif

    return NULL;
}
