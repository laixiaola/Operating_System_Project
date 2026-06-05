#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

#ifndef SIGCLD
#   define SIGCLD SIGCHLD
#endif

// 全局计时函数
static long long get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

struct {
  char *ext;
  char *filetype;
} extensions [] = {
  {"gif", "image/gif" },
  {"jpg", "image/jpg" },
  {"jpeg","image/jpeg"},
  {"png", "image/png" },
  {"ico", "image/ico" },
  {"zip", "image/zip" },
  {"gz",  "image/gz"  },
  {"tar", "image/tar" },
  {"htm", "text/html" },
  {"html","text/html" },
  {0,0} };

void logger(int type, char *s1, char *s2, int socket_fd)
{
  long long t1 = get_time_us(); // 计时开始

  int fd ;
  char logbuffer[BUFSIZE*2];
  char time_msg[128];

  switch (type) {
  case ERROR:
    sprintf(logbuffer,"ERROR: %s:%s Errno=%d pid=%d",s1, s2, errno,getpid());
    break;
  case FORBIDDEN:
    write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
    sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
    break;
  case NOTFOUND:
    write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
    sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
    break;
  case LOG:
    sprintf(logbuffer,"INFO: %s:%s:%d",s1, s2,socket_fd);
    break;
  }

  // 计时结束
  long long t2 = get_time_us();
  sprintf(time_msg, " [logger: %lld us]", t2 - t1);
  strcat(logbuffer, time_msg); // 把耗时写进日志

  if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
    write(fd,logbuffer,strlen(logbuffer));
    write(fd,"\n",1);
    close(fd);
  }

  if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

/* 处理HTTP请求：read + parse + send */
void web(int fd, int hit)
{
  long long t1 = get_time_us(); // 整个请求处理计时

  int j, file_fd, buflen;
  long i, ret, len;
  char * fstr;
  static char buffer[BUFSIZE+1];

  ret = read(fd,buffer,BUFSIZE);
  if(ret == 0 || ret == -1)
    logger(FORBIDDEN,"failed to read browser request","",fd);

  if(ret > 0 && ret < BUFSIZE)
    buffer[ret]=0;
  else buffer[0]=0;

  for(i=0;i<ret;i++)
    if(buffer[i] == '\r' || buffer[i] == '\n')
      buffer[i]='*';

  logger(LOG,"request",buffer,hit);

  if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) )
    logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);

  for(i=4;i<BUFSIZE;i++)
    if(buffer[i] == ' ') {
      buffer[i] = 0;
      break;
    }

  for(j=0;j<i-1;j++)
    if(buffer[j] == '.' && buffer[j+1] == '.')
      logger(FORBIDDEN,"Parent directory (..) not supported",buffer,fd);

  if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) )
    strcpy(buffer,"GET /index.html");

  buflen=strlen(buffer);
  fstr = 0;
  for(i=0;extensions[i].ext != 0;i++) {
    len = strlen(extensions[i].ext);
    if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
      fstr = extensions[i].filetype;
      break;
    }
  }
  if(fstr == 0)
    logger(FORBIDDEN,"file extension type not supported",buffer,fd);

  if(( file_fd = open(&buffer[5],O_RDONLY)) == -1)
    logger(NOTFOUND, "failed to open file",&buffer[5],fd);

  logger(LOG,"SEND",&buffer[5],hit);

  len = lseek(file_fd, 0, SEEK_END);
  lseek(file_fd, 0, SEEK_SET);

  sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr);
  logger(LOG,"Header",buffer,hit);

  write(fd,buffer,strlen(buffer));

  while ( (ret = read(file_fd, buffer, BUFSIZE)) > 0 )
    write(fd,buffer,ret);

  close(file_fd);
  sleep(1);
  close(fd);

  // 输出整个web函数耗时
  long long t2 = get_time_us();
  char buf[128];
  sprintf(buf, "web_process: %lld us", t2 - t1);
  logger(LOG, "WEB_DONE", buf, hit);

  exit(0);
}

int main(int argc, char **argv)
{
  int i, port, pid, listenfd, socketfd, hit;
  socklen_t length;
  static struct sockaddr_in cli_addr;
  static struct sockaddr_in serv_addr;

  if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
    printf("Use: nweb Port Directory\n");
    exit(0);
  }

  if(chdir(argv[2]) == -1){
    printf("ERROR: Can't chdir to %s\n",argv[2]);
    exit(4);
  }

  if(fork() != 0) return 0;

  signal(SIGCLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  for(i=0;i<32;i++) close(i);
  setpgrp();

  logger(LOG,"nweb starting",argv[1],getpid());

  if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
    logger(ERROR, "socket","",0);

  port = atoi(argv[1]);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
    logger(ERROR,"bind","",0);

  if(listen(listenfd,64) <0)
    logger(ERROR,"listen","",0);

  for(hit=1; ;hit++) {
    length = sizeof(cli_addr);

    // ========== accept 计时 ==========
    long long t_acc1 = get_time_us();
    socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length);
    long long t_acc2 = get_time_us();

    char acc_buf[128];
    sprintf(acc_buf, "accept: %lld us", t_acc2 - t_acc1);
    logger(LOG, "ACCEPT", acc_buf, hit);

    if(socketfd < 0) logger(ERROR,"accept","",0);

    // ========== fork 计时 ==========
    long long t_fork1 = get_time_us();
    pid = fork();
    long long t_fork2 = get_time_us();

    char fork_buf[128];
    sprintf(fork_buf, "fork: %lld us", t_fork2 - t_fork1);
    logger(LOG, "FORK", fork_buf, hit);

    if(pid < 0) logger(ERROR,"fork","",0);

    if(pid == 0) {
      close(listenfd);
      web(socketfd, hit);
    } else {
      close(socketfd);
    }
  }
}

