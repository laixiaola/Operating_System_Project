#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "pool.h"
#include "Hash.h"
#include "Cache.h"
#include "LRU.h"
#include "LFU.h"

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

// 后缀名与对应的HTTP MIME类型映射表
struct
{
  char *ext;      // 文件后缀
  char *filetype; // 对应Content-Type
} extensions[] = {
    {"gif", "image/gif"},
    {"jpg", "image/jpg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"ico", "image/ico"},
    {"zip", "image/zip"},
    {"gz", "image/gz"},
    {"tar", "image/tar"},
    {"htm", "text/html"},
    {"html", "text/html"},
    {0, 0}};

typedef struct
{
  int hit;              // 连接计数
  int fd;               // 客户端socket文件描述符
  threadpool *readpool; // 读取文件线程池
  threadpool *sendpool; // 发送响应线程池
} webparam;

typedef struct
{
  int fd;             // 客户端socket
  int hit;            // 连接计数
  char filename[256]; // 客户端请求的文件路径
  char *filetype;
  threadpool *pool; // 发送数据线程池
} readparam;

typedef struct {
    int fd;
    void *data;      // 数据副本（独立于缓存）
    size_t length;   // 数据长度
    char* filetype;
} sendparam;

void web_read_msg(void *arg);
void web_read_file(void *arg);
void web_send_msg(void *arg);

Cache *cache;

void logger(int type, char *s1, char *s2, int socket_fd)
{
  int fd;
  char logbuffer[BUFSIZE * 2];

  switch (type)
  {
  case ERROR:
    (void)sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d", s1, s2, errno, getpid());
    break;
  case FORBIDDEN:
    // 向客户端返回403禁止访问页面
    (void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n", 271);
    (void)sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
    break;
  case NOTFOUND:
    // 向客户端返回404文件不存在页面
    (void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224);
    (void)sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2);
    break;
  case LOG:
    (void)sprintf(logbuffer, " INFO: %s:%s:%d", s1, s2, socket_fd);
    break;
  }
  // 追加写入日志文件 webserver.log
  if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
  {
    (void)write(fd, logbuffer, strlen(logbuffer));
    (void)write(fd, "\n", 1);
    (void)close(fd);
  }
}

void web_read_msg(void *arg)
{
    webparam *rarg = arg;
    int fd = rarg->fd;
    int hit = rarg->hit;
    char buffer[BUFSIZE + 1];

    int ret = read(fd, buffer, BUFSIZE);
    if (ret <= 0) {
        logger(FORBIDDEN,"failed to read browser request","",fd);
        close(fd);
        free(rarg);
        return;
    }

    buffer[ret] = '\0';
    logger(LOG, "request", buffer, hit);
    
    if (strncmp(buffer, "GET ", 4)) {
        logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
        close(fd);
        free(rarg);
        return;
    }

    char *url = buffer + 4;
    char *space = strchr(url, ' ');
    if (space == NULL) {
        close(fd);
        free(rarg);
        return;
    }
    *space = '\0';

    if (strcmp(url, "/") == 0)
        strcpy(url, "/index.html");

    char *fstr = NULL;
    int buflen = strlen(url);

    for (int i = 0; extensions[i].ext != 0; i++) {
        int len = strlen(extensions[i].ext);
        if (buflen >= len && !strcmp(url + buflen - len, extensions[i].ext)) {
            fstr = extensions[i].filetype;
            break;
        }
    }

    if (fstr == NULL) {
        logger(FORBIDDEN, "file extension type not supported", url, fd);
        close(fd);
        free(rarg);
        return;
    }

    if (strstr(url, "..")) {
        logger(FORBIDDEN, "Parent directory (..) path names not supported", url, fd);
        close(fd);
        free(rarg);
        return;
    }

    char filename[256];
    snprintf(filename, sizeof(filename), ".%s", url);

    // 直接读取文件
    readparam *farg = malloc(sizeof(readparam));
    farg->fd = fd;
    farg->hit = hit;
    farg->pool = rarg->sendpool;
    farg->filetype = fstr;
    strcpy(farg->filename, filename);

    task *t = malloc(sizeof(task));
    t->function = web_read_file;
    t->arg = farg;
    t->next = NULL;
    addTask2ThreadPool(rarg->readpool, t);

    free(rarg);
}

void web_read_file(void *arg)
{
    readparam *farg = (readparam *)arg;
    int file_fd = open(farg->filename, O_RDONLY);

    if (file_fd < 0)
    {
        logger(NOTFOUND,
               "failed to open file",
               farg->filename,
               farg->fd);
        close(farg->fd);
        free(farg);
        return;
    }
    
    struct stat st;
    fstat(file_fd, &st);
    size_t filesize = st.st_size;
    
    // 读取文件到buffer
    char *buffer = malloc(filesize);
    if (buffer == NULL) {
        logger(ERROR, "malloc failed", "buffer", farg->fd);
        close(file_fd);
        close(farg->fd);
        free(farg);
        return;
    }
    
    size_t total = 0;
    while (total < filesize)
    {
        ssize_t n = read(file_fd, buffer + total, filesize - total);
        if (n <= 0) break;
        total += n;
    }
    close(file_fd);
    
    // 存入缓存（缓存拥有这份数据）
    content *cont = malloc(sizeof(content));
    cont->address = buffer;  // 缓存拥有buffer
    cont->length = filesize;
    
    Cache_Put(cache, farg->filename, cont);
    
    // ★★★ 关键：为发送线程创建独立的数据副本 ★★★
    sendparam *sarg = malloc(sizeof(sendparam));
    if (sarg == NULL) {
        logger(ERROR, "malloc failed", "sendparam", farg->fd);
        close(farg->fd);
        free(buffer);
        free(cont);
        free(farg);
        return;
    }
    
    sarg->fd = farg->fd;
    sarg->filetype = farg->filetype;
    sarg->length = filesize;
    
    sarg->data = malloc(filesize);
    if (sarg->data == NULL) {
        logger(ERROR, "malloc failed", "send data", farg->fd);
        close(farg->fd);
        free(sarg);
        free(buffer);
        free(cont);
        free(farg);
        return;
    }
    memcpy(sarg->data, buffer, filesize);
    
    // 创建发送任务
    task *t = malloc(sizeof(task));
    if (t == NULL) {
        logger(ERROR, "malloc failed", "task", farg->fd);
        close(farg->fd);
        free(sarg->data);
        free(sarg);
        free(buffer);
        free(cont);
        free(farg);
        return;
    }
    
    t->function = web_send_msg;
    t->arg = sarg;
    t->next = NULL;
    
    addTask2ThreadPool(farg->pool, t);
    free(farg);
}

void web_send_msg(void *arg)
{
    sendparam *param = (sendparam *)arg;
    
    // 构建HTTP响应头
    char header[512];
    int header_len = snprintf(header,
                 sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "Content-Type: %s\r\n"
                 "\r\n",
                 param->length,
                 param->filetype);
    
    // 发送header
    size_t sent = 0;
    while(sent < header_len)
    {
        ssize_t n = write(param->fd, header + sent, header_len - sent);
        if(n <= 0) {
            logger(ERROR, "write header failed", "", param->fd);
            break;
        }
        sent += n;
    }
    
    // 如果header发送成功，发送body
    if (sent == header_len) {
        sent = 0;
        while(sent < param->length)
        {
            ssize_t n = write(param->fd, (char*)param->data + sent, param->length - sent);
            if(n <= 0) {
                logger(ERROR, "write data failed", "", param->fd);
                break;
            }
            sent += n;
        }
    }
    
    close(param->fd);
    
    // 释放发送线程的独立数据副本
    free(param->data);
    free(param);
}

int main(int argc, char **argv)
{
  // 忽略管道破裂信号，避免客户端断开导致进程异常退出
  signal(SIGPIPE, SIG_IGN);
  int i, port, pid, listenfd, socketfd, hit;
  socklen_t length;
  static struct sockaddr_in cli_addr;  // 客户端地址结构体
  static struct sockaddr_in serv_addr; // 服务端地址结构体

  // 命令行参数校验，打印帮助信息
  if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?"))
  {
    (void)printf("hint: webserver Port-Number Top-Directory\t\tversion %d\n\n"
                 "\twebserver is a small and very safe mini web server\n"
                 "\twebserver only servers out file/web pages with extensions named below\n"
                 "\t and only from the named directory or its sub-directories.\n"
                 "\tThere is no fancy features = safe and secure.\n\n"
                 "\tExample: webserver 8181 /home/webserverdir &\n\n"
                 "\tOnly Supports:",
                 VERSION);
    for (i = 0; extensions[i].ext != 0; i++)
      (void)printf(" %s", extensions[i].ext);

    (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
                 "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
                 "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n");
    exit(0);
  }
  // 禁止使用系统敏感目录作为网站根目录
  if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5) ||
      !strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5) ||
      !strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5) ||
      !strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6))
  {
    (void)printf("ERROR: Bad top directory %s, see webserver -?\n", argv[2]);
    exit(3);
  }
  // 切换程序工作目录到指定网站根目录
  if (chdir(argv[2]) == -1)
  {
    (void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
    exit(4);
  }

  // 创建监听socket
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    logger(ERROR, "system call", "socket", 0);
  port = atoi(argv[1]);
  // 端口范围校验
  if (port < 0 || port > 60000)
    logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);

  // 初始化三组线程池：读请求、读文件、发响应
  threadpool *read_msg_pool = initThreadPool(5);
  threadpool *read_file_pool = initThreadPool(5);
  threadpool *send_msg_pool = initThreadPool(5);

  //初始化缓存
#ifdef LRUA
  ReplacementPolicy *policy =LRU_CreatePolicy(5);
#endif

#ifdef LFUA
  ReplacementPolicy *policy =LFU_CreatePolicy(5);
#endif

  cache =Cache_Create(10, policy);

  // 填充服务端地址结构
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  // 绑定端口
  if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    logger(ERROR, "system call", "bind", 0);
  if (listen(listenfd, 64) < 0)
    logger(ERROR, "system call", "listen", 0);

  // 循环接受客户端连接
  for (hit = 1;; hit++)
  {
    length = sizeof(cli_addr);
    // 阻塞等待客户端连接
    if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
      logger(ERROR, "system call", "accept", 0);

    // 封装连接参数，下发到线程池处理
    webparam *param = malloc(sizeof(webparam));
    param->hit = hit;
    param->fd = socketfd;
    param->readpool = read_file_pool;
    param->sendpool = send_msg_pool;

    task *newtask;
    newtask = (struct task *)malloc(sizeof(struct task));
    newtask->function = web_read_msg;
    newtask->arg = (void *)param;
    // 将新连接任务加入读请求线程池
    addTask2ThreadPool(read_msg_pool, newtask);
  }
}
