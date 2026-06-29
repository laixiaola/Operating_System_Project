#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/prctl.h>
#include <stdbool.h>
#include "pool.h"

#define err(str) fprintf(stderr, str)

typedef struct staconv{
	pthread_mutex_t mutex;	
	pthread_cond_t cond;	//用于阻塞和唤醒线程
	int status;		//表示任务队列状态,1表示有任务，0表示无任务
} staconv;


typedef struct taskqueue{
	pthread_mutex_t mutex;
	task *front;
	task *rear;
	staconv *has_jobs;	//根据状态阻塞线程
	int len;
} taskqueue;

typedef struct thread
{
	int id;
	pthread_t pthread;
	struct threadpool* pool;
} thread;


typedef struct threadpool{
	thread** threads;
	volatile int num_threads;
	volatile int num_working;
	pthread_mutex_t thcount_lock;
	pthread_cond_t threads_all_idle;	//用于销毁线程的条件变量
	taskqueue queue;
	volatile bool is_alive;
} threadpool;

int init_taskqueue(taskqueue* queue);
void push_taskqueue(taskqueue* queue,task* curtask);
task* take_taskqueue(taskqueue* queue);
void destroy_taskqueue(taskqueue* queue);

void staconvInit(staconv *stac, int status);
void staconvReset(staconv *stac);
void staconvPost(staconv *stac);
void staconvPostAll(staconv *stac);
void staconvWait(staconv *stac);

int create_thread(struct threadpool* pool,struct thread** pthread,int id);
void* thread_do(struct thread* pthread);
void  thread_destroy(struct thread* pthread);

struct threadpool *initThreadPool(int num_threads){
	threadpool* pool;
	pool=(threadpool*)malloc(sizeof(struct threadpool));
	pool->num_threads=0;
	pool->is_alive=1;
	pool->num_working=0;
	pthread_mutex_init(&(pool->thcount_lock),NULL);
	pthread_cond_init(&pool->threads_all_idle,NULL);
	
	if(init_taskqueue(&pool->queue)==-1){
		err("initThreadPool():Could not allocate memory for task queue\n");
		free(pool);
		return NULL;
	}

	pool->threads=(struct thread**)malloc(num_threads*sizeof(struct thread*));
	for(int i=0;i<num_threads;i++){
		create_thread(pool,&pool->threads[i],i);
	}
	while(pool->num_threads!=num_threads){}	//等待线程创建完毕
	return pool;
}



void addTask2ThreadPool(threadpool* pool,task* curtask){
	push_taskqueue(&pool->queue,curtask);
}

void waitThreadPool(threadpool* pool){
	pthread_mutex_lock(&pool->thcount_lock);
	while(pool->queue.len||pool->num_working){
		pthread_cond_wait(&pool->threads_all_idle,&pool->thcount_lock);
	}
	pthread_mutex_unlock(&pool->thcount_lock);
}

void destroyThreadPool(threadpool* pool){
	if(pool==NULL) return;
	volatile int threads_total=pool->num_threads;

	//如果当前任务队列有任务，则需等待任务队列为空，并运行线程执行任务
	pool->is_alive=0;	//结束每个线程的无限循环
	//限时1s给所有线程退出
	double TIMEOUT=1.0;
	time_t start,end;
	double tpassed=0.0;
	time(&start);
	while(tpassed<TIMEOUT && pool->num_threads){
		staconvPostAll(pool->queue.has_jobs);
		time(&end);
		tpassed=difftime(end,start);
	}
	//让剩余线程退出
	while(pool->num_threads){
		staconvPostAll(pool->queue.has_jobs);
		sleep(1);
	}

	//销毁
	destroy_taskqueue(&pool->queue);
	//销毁线程指针数组，并释放所有为线程池分配的内存
	for(int i=0;i<threads_total;i++){
		thread_destroy(pool->threads[i]);
	}
	free(pool->threads);
	free(pool);

}

int getNumofThreadWorking(threadpool* pool){
	return pool->num_working;
}

int create_thread(struct threadpool* pool,struct thread** pthread,int id){
	*pthread = (struct thread*)malloc(sizeof(struct thread));
	if(*pthread==NULL){
		err("create_thread(): Could not allocate memory for thread\n");
		return -1;
	}
	(*pthread)->pool = pool;
	(*pthread)->id =id;

	pthread_create(&(*pthread)->pthread,NULL,(void*)thread_do,(*pthread));
	pthread_detach((*pthread)->pthread);
	return 0;
}

void* thread_do(struct thread* pthread){
	char thread_name[128]={0};
	sprintf(thread_name,"thread-pool-%d",pthread->id);

	prctl(PR_SET_NAME,thread_name);

	threadpool* pool=pthread->pool;
	
	//在初始化线程池时，对已经创建的线程数量进行统计，执行pool->num_threads++
	pthread_mutex_lock(&pool->thcount_lock);
	pool->num_threads++;
	pthread_mutex_unlock(&pool->thcount_lock);



	//线程一直循环运行，直到pool->is_alive变成false
	while(pool->is_alive){
		//如果任务队列中还有任务，则继续运行；否则阻塞

		staconvWait(pool->queue.has_jobs);

		if(pool->is_alive){
			//执行到此位置，表明线程在工作，需要对工作线程数量进行统计
			//pool->num_working++;
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working++;
			pthread_mutex_unlock(&pool->thcount_lock);

			//从任务队列的队首提取任务，并执行该任务
			void (*func)(void*);
			void* arg;
			task* curtask=take_taskqueue(&pool->queue);
			if(curtask){
				func=curtask->function;
				arg=curtask->arg;
				//执行任务
				func(arg);
				//释放任务
				free(curtask);
			}

			//执行到此位置，表明线程已经将任务执行完毕，需改变工作线程数量
			//当工作线程数量为0时，表示任务全部完成，会使阻塞在waitThreadPool函数上的线程继续运行
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working--;
			if(!pool->num_working){
				pthread_cond_signal(&pool->threads_all_idle);
			}
			pthread_mutex_unlock(&pool->thcount_lock);

		}
	}
	//运行到此位置表明线程将要退出，需改变当前线程池中的线程数量
	//pool->num_threads--;

	pthread_mutex_lock(&pool->thcount_lock);
	pool->num_threads--;
	pthread_mutex_unlock(&pool->thcount_lock);

	return NULL;
}

void thread_destroy(thread* pthread){
	free(pthread);
}

//初始化任务队列
int init_taskqueue(taskqueue* queue){
	queue->len=0;
	queue->front=NULL;
	queue->rear=NULL;

	queue->has_jobs=(struct staconv*)malloc(sizeof(struct staconv));
	if(queue->has_jobs==NULL){
		return -1;
	}
	pthread_mutex_init(&(queue->mutex),NULL);
	staconvInit(queue->has_jobs,0);

	return 0;
}

//add task to queue
void push_taskqueue(taskqueue* queue,task* curtask){
	pthread_mutex_lock(&queue->mutex);
	curtask->next=NULL;

	switch(queue->len){
		case 0:		//队列中无任务
			queue->front=curtask;
			queue->rear=curtask;
			break;
		default:	//队列中有任务
			queue->rear->next=curtask;
			queue->rear=curtask;
	}

	queue->len++;
	staconvPost(queue->has_jobs);
	pthread_mutex_unlock(&queue->mutex);
}

//取出一个任务
task* take_taskqueue(taskqueue* queue){
	pthread_mutex_lock(&queue->mutex);
	task* ptask=queue->front;
	switch(queue->len){
		case 0:	//队列中无任务
			break;
		case 1:	//队列中只有一个任务
			queue->front=NULL;
			queue->rear=NULL;
			queue->len=0;
			break;
		default:	//队列中有多个任务
			queue->front=ptask->next;
			queue->len--;
			staconvPost(queue->has_jobs);
	}
	pthread_mutex_unlock(&queue->mutex);
	return ptask;
}

//回收所有队列所占空间
void destroy_taskqueue(taskqueue* queue){
	//清空队列
	while(queue->len){
		free(take_taskqueue(queue));
	}
	free(queue->has_jobs);
}

void staconvInit(staconv *stac, int status){
	if(status<0||status>1){
		err("staconvInit():Binary semaphore can take only values 1 or 0");
		exit(1);
	}
	pthread_mutex_init(&(stac->mutex),NULL);
	pthread_cond_init(&(stac->cond),NULL);
	stac->status=status;
}

void staconvReset(staconv *stac){
	pthread_mutex_destroy(&(stac->mutex));
	pthread_cond_destroy(&(stac->cond));
	staconvInit(stac,0);
}

void staconvPost(staconv *stac){
	pthread_mutex_lock(&stac->mutex);
	stac->status=1;
	pthread_cond_signal(&stac->cond);
	pthread_mutex_unlock(&stac->mutex);
}

void staconvPostAll(staconv *stac){
	pthread_mutex_lock(&stac->mutex);
	stac->status=1;
	pthread_cond_broadcast(&stac->cond);
	pthread_mutex_unlock(&stac->mutex);
}

void staconvWait(staconv *stac){
	pthread_mutex_lock(&stac->mutex);
	while(stac->status!=1){
		pthread_cond_wait(&stac->cond,&stac->mutex);
	}
	stac->status=0;
	pthread_mutex_unlock(&stac->mutex);
}
