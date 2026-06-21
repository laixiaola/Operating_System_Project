#ifndef POOL_H
#define POOL_H
typedef struct threadpool threadpool;
typedef struct task{
	struct task* next;
	void (*function)(void* arg);	//函数指针
	void* arg;
} task;


struct threadpool *initThreadPool(int num_threads);
void addTask2ThreadPool(threadpool* pool,task* curtask);
void waitThreadPool(threadpool* pool);
void destroyThreadPool(threadpool* pool);

#endif
