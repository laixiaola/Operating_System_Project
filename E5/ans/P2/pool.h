#ifndef POOL_H
#define POOL_H
typedef struct staconv{
	pthread_mutex_t mutex;	
	pthread_cond_t cond;	//用于阻塞和唤醒线程
	int status;		//表示任务队列状态,1表示有任务，0表示无任务
} staconv;

typedef struct task{
	struct task* next;
	void (*function)(void* arg);	//函数指针
	void* arg;
} task;

typedef struct taskqueue{
	pthread_mutex_t mutex;
	task *front;
	task *rear;
	staconv *has_jobs;	//根据状态阻塞线程
	struct threadpool *pool;
	int len;
} taskqueue;

typedef struct thread
{
	int id;
	pthread_t pthread;
	struct threadpool* pool;
} thread;

typedef struct pool_stat
{
    long long total_work_time_us;      // 总工作时间
    long long total_wait_time_us;      // 总阻塞时间

    long long work_count;              // 执行任务次数

    int max_working;                   // 最大活跃线程数
    int min_working;                   // 最小活跃线程数

	//平均活跃数
	long long working_sum;
    long long sample_count;
	
	int max_queue_len;
	int queue_sample_count;
	int queue_len_sum;
} pool_stat;

typedef struct threadpool{
	thread** threads;
	volatile int num_threads;
	volatile int num_working;
	pthread_mutex_t thcount_lock;
	pthread_cond_t threads_all_idle;	//用于销毁线程的条件变量
	taskqueue queue;
	pool_stat stat;
	volatile bool is_alive;
} threadpool;


struct threadpool *initThreadPool(int num_threads);
void addTask2ThreadPool(threadpool* pool,task* curtask);
void waitThreadPool(threadpool* pool);
void destroyThreadPool(threadpool* pool);

#endif
