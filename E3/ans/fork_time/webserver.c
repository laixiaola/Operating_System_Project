#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>   //获取时间
#include <sys/time.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/wait.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif



struct {
    char *ext;
    char *filetype;
} extensions [] = {
    {"gif",  "image/gif"  },
    {"jpg",  "image/jpg"  },
    {"jpeg", "image/jpeg" },
    {"png",  "image/png"  },
    {"ico",  "image/ico"  },
    {"zip",  "image/zip"  },
    {"gz",   "image/gz"   },
    {"tar",  "image/tar"  },
    {"htm",  "text/html"  },
    {"html", "text/html"  },
    {0, 0}
};

typedef struct{
	void *ptr;
	sem_t *sem;
	int shm_fd;
	char shm_name[32];
	char sem_name[32];
} Share;

// 初始化共享内存
Share* init_shared_memory(char* shname,char* semname) {
    Share *sh = malloc(sizeof(Share));
    if (!sh) return NULL;
    
    strcpy(sh->shm_name, shname);
    strcpy(sh->sem_name, semname);
    
    // 创建共享内存
    sh->shm_fd = shm_open(sh->shm_name, O_RDWR | O_CREAT, 0666);
    if (sh->shm_fd == -1) {
        perror("shm_open");
        free(sh);
        return NULL;
    }
    
    if (ftruncate(sh->shm_fd, sizeof(long long)) == -1) {
        perror("ftruncate");
        close(sh->shm_fd);
        free(sh);
        return NULL;
    }
    
    sh->ptr = mmap(NULL, sizeof(long long), 
                   PROT_READ | PROT_WRITE, 
                   MAP_SHARED, sh->shm_fd, 0);
    if (sh->ptr == MAP_FAILED) {
        perror("mmap");
        close(sh->shm_fd);
        free(sh);
        return NULL;
    }
    
    // 创建信号量
    sh->sem = sem_open(sh->sem_name, O_CREAT, 0666, 1);
    if (sh->sem == SEM_FAILED) {
        perror("sem_open");
        munmap(sh->ptr, sizeof(long long));
        close(sh->shm_fd);
        free(sh);
        return NULL;
    }
    
    // 初始化数据
    *(long long*)sh->ptr=0;
    
    return sh;
}

void sigchld_handler(int sig)
{
    // 循环收割所有已退出子进程，防止瞬间多子进程遗漏
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void print_time(int hit,Share *skr,Share *skw,Share *lgw,Share *flr,Share *tt){
	long long skrt,skwt,lgrt,flrt,total;
	sem_wait(lgw->sem);lgrt=*(long long*)lgw->ptr;sem_post(lgw->sem);
	sem_wait(skr->sem);skrt=*(long long*)skr->ptr;sem_post(skr->sem);
	sem_wait(skw->sem);skwt=*(long long*)skw->ptr;sem_post(skw->sem);
	sem_wait(flr->sem);flrt=*(long long*)flr->ptr;sem_post(flr->sem);
	sem_wait(tt->sem);total=*(long long*)tt->ptr ;sem_post(tt->sem);
	printf("共用%lldus(%lldms)成功处理%d个客户端请求，其中\n",total,total/1000,hit);
	printf("    平均每个客户端完成请求处理时间为%lldus(%lldms)。\n",total/hit,(total/hit)/1000);
	printf("    平均每个客户端完成读socket时间为%lldus(%lldms)。\n",skrt/hit,(skrt/hit)/1000);
	printf("    平均每个客户端完成写socket时间为%lldus。\n",skwt/hit);
	printf("    平均每个客户端完成读网页数据时间为%lldus。\n",flrt/hit);
	printf("    平均每个客户端完成写日志数据时间为%lldus。\n",lgrt/hit);
}

// 获取当前时间
static long long get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* 日志函数，将运行过程中的提示信息记录到 webserver.log 文件中*/
void logger(int type, char *s1, char *s2, int socket_fd,Share *sh)
{
    int fd;
    char logbuffer[BUFSIZE * 2];

    //获取当前时间用于日志输出
  time_t now=time(NULL);
  char *time_str=asctime(localtime(&now));
  time_str[strlen(time_str)-1]=0;

    /*根据消息类型，将消息放入 logbuffer 缓存，或直接将消息通过 socket 通道返回给客户端*/
    switch (type) {
        case ERROR:
            (void)sprintf(logbuffer, "[%s] ERROR: %s:%s Errno=%d exiting pid=%d", time_str, s1, s2, errno, getpid());
            break;
        case FORBIDDEN:
            (void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\n The requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n", 271);
            (void)sprintf(logbuffer, "[%s] FORBIDDEN: %s:%s", time_str, s1, s2);
            break;
        case NOTFOUND:
            (void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224);
            (void)sprintf(logbuffer, "[%s] NOT FOUND: %s:%s", time_str, s1, s2);
            break;
        case LOG:
            (void)sprintf(logbuffer, "[%s] INFO: %s:%s:%d", time_str, s1, s2, socket_fd);
            break;
    }

    /* 将 logbuffer 缓存中的消息存入 webserver.log 文件*/
      long long log_start=get_time();
    if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0) {
        (void)write(fd, logbuffer, strlen(logbuffer));
        (void)write(fd, "\n", 1);
        (void)close(fd);
    }
      long long log_cost=get_time()-log_start;
      sem_wait(sh->sem);
      *(long long*)sh->ptr+=log_cost;
      sem_post(sh->sem);
}

// 计时日志
void log_time(const char *step, long long usec) {
    char buf[1024];
    time_t now = time(NULL);
    char *time_str = asctime(localtime(&now));
    time_str[strlen(time_str)-1] = 0;

    sprintf(buf, "[%s] TIME: %-20s %lld ms (%.3f s)\n",
            time_str, step, usec, usec / 1000.0);

    int fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, buf, strlen(buf));
        close(fd);
    }
}

/* 此函数完成了 WebServer 主要功能，它首先解析客户端发送的消息，然后从中获取客户端请求的文件名，
然后根据文件名从本地将此文件读入缓存，并生成相应的 HTTP 响应消息；
最后通过服务器与客户端的 socket 通道向客户端返回 HTTP 响应消息*/
void web(int fd, int hit,Share *skr,Share *skw,Share *lgw,Share *flr,Share *tt,sem_t *print_sem)
{
  long long start_time=get_time();
    int j, file_fd, buflen;
    long i, ret, len;
    char *fstr;
    char buffer[BUFSIZE + 1]; /* 设置静态缓冲区 */
    
      long long socket_write_time=0,file_read_time=0;
    
    long long socket_read_start=get_time();
    ret = read(fd, buffer, BUFSIZE); /* 从连接通道中读取客户端的请求消息 */
    long long socket_read_cost=get_time()-socket_read_start;
    sem_wait(skr->sem);
    *(long long*)skr->ptr+=socket_read_cost;
    sem_post(skr->sem);
    
    
    if (ret == 0 || ret == -1) {
        //如果读取客户端消息失败，则向客户端发送 HTTP 失败响应信息
        logger(FORBIDDEN, "failed to read browser request", "", fd,lgw);
    }

    if (ret > 0 && ret < BUFSIZE) {
        /* 设置有效字符串，即将字符串尾部表示为 0 */
        buffer[ret] = 0;
    } else {
        buffer[0] = 0;
    }

    for (i = 0; i < ret; i++) {
        /* 移除消息字符串中的“CF” 和“LF” 字符*/
        if (buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i] = '*';
    }

    logger(LOG, "request", buffer, hit,lgw);

    /*判断客户端 HTTP 请求消息是否为 GET 类型，如果不是则给出相应的响应消息*/
    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd,lgw);
    }

    for (i = 4; i < BUFSIZE; i++) {
        /* null terminate after the second space to ignore extra stuff */
        if (buffer[i] == ' ') {
            /* string is "GET URL " +lots of other stuff */
            buffer[i] = 0;
            break;
        }
    }

    for (j = 0; j < i - 1; j++) {
        /* 在消息中检测路径，不允许路径中出现“.” */
        if (buffer[j] == '.' && buffer[j + 1] == '.') {
            logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd,lgw);
        }
    }

    if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6)) {
        /* 如果请求消息中没有包含有效的文件名，则使用默认的文件名 index.html */
        (void)strcpy(buffer, "GET /index.html");
    }

    /* 根据预定义在 extensions 中的文件类型，检查请求的文件类型是否本服务器支持 */
    buflen = strlen(buffer);
    fstr = (char *)0;

    for (i = 0; extensions[i].ext != 0; i++) {
        len = strlen(extensions[i].ext);
        if (!strncmp(&buffer[buflen - len], extensions[i].ext, len)) {
            fstr = extensions[i].filetype;
            break;
        }
    }

    if (fstr == 0) {
        logger(FORBIDDEN, "file extension type not supported", buffer, fd,lgw);
    }

    if ((file_fd = open(&buffer[5], O_RDONLY)) == -1) {
        /* 打开指定的文件名*/
        logger(NOTFOUND, "failed to open file", &buffer[5], fd,lgw);
    }

    logger(LOG, "SEND", &buffer[5], hit,lgw);

    len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* 通过 lseek 获取文件长度*/
    (void)lseek(file_fd, (off_t)0, SEEK_SET);       /* 将文件指针移到文件首位置*/

    (void)sprintf(buffer, "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
    logger(LOG, "Header", buffer, hit,lgw);

    (void)write(fd, buffer, strlen(buffer));

    /* 不停地从文件里读取文件内容，并通过 socket 通道向客户端返回文件内容*/
 	 while ( 1 ) {
	  long long file_read_start=get_time(); 	 
 	  ret = read(file_fd, buffer, BUFSIZE);
 	  long long file_read_cost=get_time()-file_read_start;
 	  file_read_time+=file_read_cost;
 	  if(ret<=0) break;
 	  
 	  long long socket_write_start=get_time();
  	  (void)write(fd,buffer,ret);
  	  long long socket_write_cost=get_time()-socket_write_start;
  	  socket_write_time+=socket_write_cost;
 	 }
    sem_wait(skw->sem);
    *(long long*)skw->ptr+=socket_write_time;
    sem_post(skw->sem);
    sem_wait(flr->sem);
    *(long long*)flr->ptr+=file_read_time;
    sem_post(flr->sem);
    
    //sleep(1); /* sleep 的作用是防止消息未发出，已经将此 socket 通道关闭*/
    close(fd);
    
    long long end_time=get_time()-start_time;
    sem_wait(tt->sem);
    *(long long*)tt->ptr+=end_time;
    sem_post(tt->sem);
    
    sem_wait(print_sem);
    print_time(hit,skr,skw,lgw,flr,tt);
    sem_post(print_sem);
}

int main(int argc, char **argv)
{
    int i, port, listenfd, socketfd, hit;
    socklen_t length;
    static struct sockaddr_in cli_addr;  /* static = initialised to zeros */
    static struct sockaddr_in serv_addr; /* static = initialised to zeros */
    
    Share *socket_read=init_shared_memory("skrt","skrtsem");
    Share *socket_write=init_shared_memory("skwt","skwtsem");
    Share *log_write=init_shared_memory("lgrt","lgrtsem");
    Share *file_read=init_shared_memory("flrt","flrtsem");
    Share *total_time=init_shared_memory("ttt","tttsem");
    
    sem_t *print_sem;
    sem_open("print", O_CREAT, 0666, 1);
    
    /*解析命令参数*/
    if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?")) {
        (void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
                     "\tnweb is a small and very safe mini web server\n"
                     "\tnweb only servers out file/web pages with extensions named below\n"
                     "\t and only from the named directory or its sub-directories.\n"
                     "\tThere is no fancy features = safe and secure.\n\n"
                     "\tExample:webserver 8181 /home/nwebdir &\n\n"
                     "\tOnly Supports:", VERSION);

        for (i = 0; extensions[i].ext != 0; i++)
            (void)printf(" %s", extensions[i].ext);

        (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
                     "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
                     "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n");
        exit(0);
    }

    if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5) ||
        !strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5) ||
        !strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5) ||
        !strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6)) {
        (void)printf("ERROR: Bad top directory %s, see nweb -?\n", argv[2]);
        exit(3);
    }

    if (chdir(argv[2]) == -1) {
        (void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
        exit(4);
    }
    
    
    /* 建立服务端侦听 socket*/
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        logger(ERROR, "system call", "socket", 0,log_write);

    port = atoi(argv[1]);
    if (port < 0 || port > 60000)
        logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0,log_write);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    signal(SIGCHLD, sigchld_handler);
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        logger(ERROR, "system call", "bind", 0,log_write);

    if (listen(listenfd, 64) < 0)
        logger(ERROR, "system call", "listen", 0,log_write);

    for (hit = 1;; hit++) {
        length = sizeof(cli_addr);
        if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
            logger(ERROR, "system call", "accept", 0,log_write);
	
	//创建多进程
	int pid=fork();
	if(pid<0) logger(ERROR,"system call","fork",0,log_write);
	else if(pid==0){	//子进程
		(void)close(listenfd);
        	web(socketfd, hit,socket_read,socket_write,log_write,file_read,total_time,print_sem);

		exit(0);
        }
        else{
        	(void)close(socketfd);	//父进程

        }
    }
}

