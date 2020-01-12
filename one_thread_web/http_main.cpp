#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<iostream>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>
#include<sys/epoll.h>
#include<memory>
#include<pthread.h>
#include<exception>
#include<semaphore.h>
#include "http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 100000
#define PORT 10086

extern int addfd(int epollfd, int fd);
extern int removefd(int epollfd, int fd);

//信号处理函数
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
    printf("pid = %d\n", getpid());
    //忽略SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);

    /*
    //创建线程池
    threadpool<http_conn>* pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch(...)
    {
        return 1;
    }
    */
    //预先为每个可能的客户连接分配一个http_conn对象
    http_conn *users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp = {1, 0}; //避免time_wait状态，断开连接时发送RST分节断开。
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in serv_adr;
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_port = htons(PORT);
    serv_adr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(listenfd, (sockaddr*)&serv_adr, sizeof(serv_adr));
    assert(ret >= 0);

    ret = listen(listenfd, SOMAXCONN);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd);
    http_conn::m_epollfd = epollfd;

    while(true)
    {
        printf("-----\n");
        int cond = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        printf("=======\n");
        if((cond < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for(int i = 0; i < cond; ++i)
        {
            int fd = events[i].data.fd;
            //有新的连接
            if(fd == listenfd)
            {
                struct sockaddr_in cli_addr;
                socklen_t cli_len = sizeof(cli_addr);
                //循环调用accpet，因为ET模式下当多个连接到达时只会触发一次listenfd可读事件
                while(true)
                {
                    int connfd = accept(listenfd, (sockaddr*)&cli_addr, &cli_len);
                    if(connfd < 0)
                    {
                        if(errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        else 
                        {
                            printf("errno is: %d\n", errno);
                            exit(1);
                        }
                    }
                    printf("有新的连接%d到来\n", connfd);
                    //超过最大连接上限
                    if(http_conn::m_user_count >= MAX_FD)
                    {
                        show_error(connfd, "Internal server busy");
                        continue;
                    }
                    //初始化客户连接
                    users[connfd].init(connfd, cli_addr);
                }
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //如果有异常，直接关闭客户连接
                users[fd].close_conn();
            }
            //可读事件
            else if(events[i].events & EPOLLIN)
            {
                std::cout << "可读事件触发\n";
                //users[fd].process(); //改进之后效率提升明显
                
                //根据读的结果，决定是将任务添加到线程池，还是关闭连接
                if(users[fd].read())
                {
                    std::cout << "将任务加入到工作队列中\n";
                    users[fd].process();    //单线程模型
                }
                else
                {
                    users[fd].close_conn();
                }
                
                
            }
            //可写事件
            else if(events[i].events & EPOLLOUT)
            {
                //根据写的结果，决定是否关闭连接
                if(!users[fd].write())
                {
                    users[fd].close_conn();
                }
            }
            else
            {
                std::cout << "欢迎来到德莱联盟\n";   
            }
        }
    }   
    close(epollfd);
    close(listenfd);
    delete [] users;
    return 0;
}
