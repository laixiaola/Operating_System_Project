/*Server Code*/
/* webserver.c*/
/*The following main code from https://github.com/ankushagarwal/nweb, but they are modified slightly*/
/* 增加了高精度计时，用于定位性能瓶颈 */

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
#include <time.h>
#include <sys/time.h>   // 用于计时

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

// 计时函数
static long long get_usec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

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

/* 日志函数，将运行过程中的提示信息记录到 webserver.log 文件中*/
void logger(int type, char *s1, char *s2, int socket_fd)
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
    if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0) {
        (void)write(fd, logbuffer, strlen(logbuffer));
        (void)write(fd, "\n", 1);
        (void)close(fd);
    }
}

// 计时日志
void log_time(const char *step, long long usec) {
    char buf[1024];
    time_t now = time(NULL);
    char *time_str = asctime(localtime(&now));
    time_str[strlen(time_str)-1] = 0;

    sprintf(buf, "[%s] TIME: %-20s %lld us (%.3f ms)\n",
            time_str, step, usec, usec / 1000.0);

    int fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, buf, strlen(buf));
        close(fd);
    }
}

/* 此函数完成了 WebServer 主要功能 */
void web(int fd, int hit)
{
    int j, file_fd, buflen;
    long i, ret, len;
    char *fstr;
    static char buffer[BUFSIZE + 1];

    long long t0, t1, t2, t3, t4, t5, t6;
    t0 = get_usec();

    // 1. 读取浏览器请求
    ret = read(fd, buffer, BUFSIZE);
    t1 = get_usec();
    
    log_time("read_request", t1 - t0);

    if (ret == 0 || ret == -1) {
        logger(FORBIDDEN, "failed to read browser request", "", fd);
        close(fd);
        return;
    }

    if (ret > 0 && ret < BUFSIZE)
        buffer[ret] = 0;
    else
        buffer[0] = 0;

    for (i = 0; i < ret; i++)
        if (buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i] = '*';

    logger(LOG, "request", buffer, hit);

    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
        close(fd);
        return;
    }

    for (i = 4; i < BUFSIZE; i++) {
        if (buffer[i] == ' ') {
            buffer[i] = 0;
            break;
        }
    }

    for (j = 0; j < i - 1; j++) {
        if (buffer[j] == '.' && buffer[j + 1] == '.') {
            logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd);
            close(fd);
            return;
        }
    }

    if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6)) {
        (void)strcpy(buffer, "GET /index.html");
    }

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
        logger(FORBIDDEN, "file extension type not supported", buffer, fd);
        close(fd);
        return;
    }

    // 2. 打开文件
    t2 = get_usec();
    if ((file_fd = open(&buffer[5], O_RDONLY)) == -1) {
        logger(NOTFOUND, "failed to open file", &buffer[5], fd);
        close(fd);
        return;
    }
    t3 = get_usec();
    log_time("open_file", t3 - t2);

    logger(LOG, "SEND", &buffer[5], hit);

    len = (long)lseek(file_fd, (off_t)0, SEEK_END);
    (void)lseek(file_fd, (off_t)0, SEEK_SET);

    // 3. 拼接并发送HTTP头
    (void)sprintf(buffer, "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr);
    logger(LOG, "Header", buffer, hit);

    t4 = get_usec();
    (void)write(fd, buffer, strlen(buffer));
    t5 = get_usec();
    log_time("send_header", t5 - t4);

    // 4. 发送文件内容
    while ((ret = read(file_fd, buffer, BUFSIZE)) > 0) {
        (void)write(fd, buffer, ret);
    }
    t6 = get_usec();
    log_time("send_file_data", t6 - t5);

    // 总耗时
    log_time("TOTAL_PROCESS_TIME", t6 - t0);

    close(file_fd);
    sleep(1);
    close(fd);
}

int main(int argc, char **argv)
{
    int i, port, listenfd, socketfd, hit;
    socklen_t length;
    static struct sockaddr_in cli_addr;
    static struct sockaddr_in serv_addr;

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

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        logger(ERROR, "system call", "socket", 0);

    port = atoi(argv[1]);
    if (port < 0 || port > 60000)
        logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        logger(ERROR, "system call", "bind", 0);

    if (listen(listenfd, 64) < 0)
        logger(ERROR, "system call", "listen", 0);

    long long t_accept_start;

    for (hit = 1;; hit++) {
        length = sizeof(cli_addr);

        // 计时 accept 等待连接
        t_accept_start = get_usec();
        if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
            logger(ERROR, "system call", "accept", 0);
        log_time("accept_wait", get_usec() - t_accept_start);

        web(socketfd, hit);
    }
}

