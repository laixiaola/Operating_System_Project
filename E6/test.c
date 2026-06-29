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
#include "Hash.h"
#ifdef HASHTEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#ifdef HASHTHREADED
#include <pthread.h>
#include <semaphore.h>
#endif

#ifdef _WIN32
static inline void timersub(
	const struct timeval *t1,
	const struct timeval *t2,
	struct timeval *res)
{
	res->tv_sec = t1->tv_sec - t2->tv_sec;
	if( t1->tv_usec - t2->tv_usec < 0 ) {
		res->tv_sec--;
		res->tv_usec = t1->tv_usec - t2->tv_usec + 1000 * 1000;
	} else {
		res->tv_usec = t1->tv_usec - t2->tv_usec;
	}
}
#endif


#ifdef HASHTHREADED

#define NUMTHREADS 8
#define HASHCOUNT 1000000

typedef struct threadinfo {hashtable *table; int start;} threadinfo;
void * thread_func(void *arg)
{
	threadinfo *info = arg;
	char buffer[512];
	int i = info->start;
	hashtable *table = info->table;
	free(info);
	for(;i<HASHCOUNT;i+=NUMTHREADS) {
		sprintf(buffer,"%d",i);
		content* cont=malloc(sizeof(content));
		cont->length=rand()%2048;
		cont->address=malloc(cont->length);
		addItem(table,buffer,cont);
	}
}


int main()
{
	// create
	hashtable * table = create_hash(HASHCOUNT);

	// hash a million strings into various sizes of table
	struct timeval tval_before, tval_done1, tval_done2, tval_writehash, tval_readhash;
	gettimeofday(&tval_before, NULL);
	int t;
	pthread_t * threads[NUMTHREADS];
	for(t=0;t<NUMTHREADS;++t) {
		pthread_t * pth = malloc(sizeof(pthread_t));
		threads[t] = pth;
		threadinfo *info = (threadinfo*)malloc(sizeof(threadinfo));
		info->table = table; info->start = t;
		pthread_create(pth,NULL,thread_func,info);
	}
	for(t=0;t<NUMTHREADS;++t) {
		pthread_join(*threads[t], NULL);
	}
	gettimeofday(&tval_done1, NULL);
	int i,j;
	int error = 0;
	char buffer[512];
	for(i=0;i<HASHCOUNT;++i) {
		sprintf(buffer,"%d",i);
		getContentByKey(table,buffer);
	}
	gettimeofday(&tval_done2, NULL);
	timersub(&tval_done1, &tval_before, &tval_writehash);
	timersub(&tval_done2, &tval_done1, &tval_readhash);
	printf("\n%d threads.\n",NUMTHREADS);
	printf("Store %d ints by string: %ld.%06ld sec, read %d ints: %ld.%06ld sec\n",HASHCOUNT,
		(long int)tval_writehash.tv_sec, (long int)tval_writehash.tv_usec,HASHCOUNT,
		(long int)tval_readhash.tv_sec, (long int)tval_readhash.tv_usec);
	delete_hash(table);
	return 0;
}

#endif
#endif
