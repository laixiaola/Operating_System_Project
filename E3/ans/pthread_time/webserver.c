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
#include <sys/time.h>
#include <time.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

#ifndef SIGCLD
#   define SIGCLD SIGCHLD
#endif

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
  
typedef struct{
	int hit;
	int fd;
}webparam;

static long long get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

long long socket_read_total,socket_write_total,log_total,file_read_total,total_time;
pthread_mutex_t srt = PTHREAD_MUTEX_INITIALIZER;  //socket read time
pthread_mutex_t swt = PTHREAD_MUTEX_INITIALIZER;  //socket write time
pthread_mutex_t lgt = PTHREAD_MUTEX_INITIALIZER;  //log write time
pthread_mutex_t frt = PTHREAD_MUTEX_INITIALIZER;  //file read time
pthread_mutex_t ttt = PTHREAD_MUTEX_INITIALIZER;  //total time
pthread_mutex_t prt = PTHREAD_MUTEX_INITIALIZER;  //print mutex


void print_time(int hit){
	long long skrt,skwt,lgrt,flrt,total;
	pthread_mutex_lock(&lgt);lgrt=log_total;pthread_mutex_unlock(&lgt);
	pthread_mutex_lock(&srt);skrt=socket_read_total;pthread_mutex_unlock(&srt);
	pthread_mutex_lock(&swt);skwt=socket_write_total;pthread_mutex_unlock(&swt);
	pthread_mutex_lock(&frt);flrt=file_read_total;pthread_mutex_unlock(&frt);
	pthread_mutex_lock(&ttt);total=total_time;pthread_mutex_unlock(&ttt);
	printf("共用%lldus(%lldms)成功处理%d个客户端请求，其中\n",total,total/1000,hit);
	printf("    平均每个客户端完成请求处理时间为%lldus(%lldms)。\n",total/hit,(total/hit)/1000);
	printf("    平均每个客户端完成读socket时间为%lldus(%lldms)。\n",skrt/hit,(skrt/hit)/1000);
	printf("    平均每个客户端完成写socket时间为%lldus。\n",skwt/hit);
	printf("    平均每个客户端完成读网页数据时间为%lldus。\n",flrt/hit);
	printf("    平均每个客户端完成写日志数据时间为%lldus。\n",lgrt/hit);
}

//unsigned long get_file_size(const char *path){
//	unsigned long filesize=-1;
//	struct stat statbuff;
//	if(stat(path,&statbuff)<0){
//		return filesize;
//	}
//	else{
//		filesize=statbuff.st_size;
//	}
//	return filesize;
//}

void logger(int type, char *s1, char *s2, int socket_fd)
{
  int fd ;
  char logbuffer[BUFSIZE*2];

  switch (type) {
  case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid());
    break;
  case FORBIDDEN:
    (void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
    (void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
    break;
  case NOTFOUND:
    (void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
    (void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
    break;
  case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
  }
  
  /* No checks here, nothing can be done with a failure anyway */
  long long log_start=get_time();
  if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
    (void)write(fd,logbuffer,strlen(logbuffer));
    (void)write(fd,"\n",1);
    (void)close(fd);
  }
  long long log_cost=get_time()-log_start;
  pthread_mutex_lock(&lgt);
  log_total+=log_cost;
  pthread_mutex_unlock(&lgt);
        
  //if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}


void* web(void* data)
{
  long long start_time=get_time();
  int fd,hit;
  webparam *param=(webparam*)data;
  fd=param->fd;
  hit=param->hit;
  
  int j, file_fd, buflen;
  long i, ret, len;
  char * fstr;
  char buffer[BUFSIZE+1]; /* 缓存 */
  
  long long socket_write_time=0,file_read_time=0;
  
  long long socket_read_start=get_time();
  ret =read(fd,buffer,BUFSIZE);   /* read Web request in one go */
  long long socket_read_cost=get_time()-socket_read_start;
  
  pthread_mutex_lock(&srt);
  socket_read_total+=socket_read_cost;
  pthread_mutex_unlock(&srt);
  
  if(ret == 0 || ret == -1) {  /* read failure stop now */
    logger(FORBIDDEN,"failed to read browser request","",fd);
  }
  else{
  	if(ret > 0 && ret < BUFSIZE)  /* return code is valid chars */
 	   buffer[ret]=0;    /* terminate the buffer */
  	else buffer[0]=0;
 	 for(i=0;i<ret;i++)  /* remove CF and LF characters */
  	  if(buffer[i] == '\r' || buffer[i] == '\n')
  	    buffer[i]='*';
	  logger(LOG,"request",buffer,hit);
 	 if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
 	   logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
 	 }
 	 for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
  	  if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
   	   buffer[i] = 0;
  	    break;
  	  }
 	 }
 	 for(j=0;j<i-1;j++)   /* check for illegal parent directory use .. */
 	   if(buffer[j] == '.' && buffer[j+1] == '.') {
  	    logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
 	   }
 	 if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
  	  (void)strcpy(buffer,"GET /index.html");

 	 /* work out the file type and check we support it */
 	 buflen=strlen(buffer);
  	fstr = (char *)0;
  	for(i=0;extensions[i].ext != 0;i++) {
   	 len = strlen(extensions[i].ext);
   	 if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
  	    fstr =extensions[i].filetype;
  	    break;
 	   }
	  }
 	 if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

 	 if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
  	  logger(NOTFOUND, "failed to open file",&buffer[5],fd);
  	  close(fd);
  	  free(param);
  	  return NULL;
 	 }
 	 logger(LOG,"SEND",&buffer[5],hit);
  	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
     	   (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
      	    (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: webserver/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
  	logger(LOG,"Header",buffer,hit);
 	 (void)write(fd,buffer,strlen(buffer));

 	 /* send file in 8KB block - last block may be smaller */
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
 	 pthread_mutex_lock(&swt);
  	 socket_write_total+=socket_write_time;
 	 pthread_mutex_unlock(&swt);
	 pthread_mutex_lock(&frt);
	 file_read_total+=file_read_time;
	 pthread_mutex_unlock(&frt);
 	 //usleep(1000);
 	 close(file_fd);
  }
  close(fd);
  free(param);
  
  long long end_time=get_time()-start_time;
  pthread_mutex_lock(&ttt);
  total_time+=end_time;
  pthread_mutex_unlock(&ttt);
  
  pthread_mutex_lock(&prt);
  print_time(hit);
  pthread_mutex_unlock(&prt);

}

int main(int argc, char **argv)
{
  int i, port, pid, listenfd, socketfd, hit;
  socklen_t length;
  static struct sockaddr_in cli_addr; /* static = initialised to zeros */
  static struct sockaddr_in serv_addr; /* static = initialised to zeros */
  


  if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
    (void)printf("hint: webserver Port-Number Top-Directory\t\tversion %d\n\n"
  "\twebserver is a small and very safe mini web server\n"
  "\twebserver only servers out file/web pages with extensions named below\n"
  "\t and only from the named directory or its sub-directories.\n"
  "\tThere is no fancy features = safe and secure.\n\n"
  "\tExample: webserver 8181 /home/webserverdir &\n\n"
  "\tOnly Supports:", VERSION);
    for(i=0;extensions[i].ext != 0;i++)
      (void)printf(" %s",extensions[i].ext);

    (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
  "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
  "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
    exit(0);
  }
  if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
      !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
      !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
      !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
    (void)printf("ERROR: Bad top directory %s, see webserver -?\n",argv[2]);
    exit(3);
  }
  if(chdir(argv[2]) == -1){
    (void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
    exit(4);
  }
  


  /* setup the network socket */
  if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
    logger(ERROR, "system call","socket",0);
  port = atoi(argv[1]);
  if(port < 0 || port >60000)
    logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
    
    //初始化线程属性
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);	//线程分离
  pthread_t pth;
  
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
    logger(ERROR,"system call","bind",0);
  if( listen(listenfd,64) <0)
    logger(ERROR,"system call","listen",0);
  for(hit=1; ;hit++) {
    length = sizeof(cli_addr);
    if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
      logger(ERROR,"system call","accept",0);
    webparam *param=malloc(sizeof(webparam));
    param->hit=hit;
    param->fd=socketfd;
    if(pthread_create(&pth,&attr,&web,(void*)param)<0){
    	logger(ERROR,"system call","pthread_create",0);
    }
  }
}
