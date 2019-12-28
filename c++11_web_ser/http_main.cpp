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
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536   
#define MAX_EVENT_NUMBER 10000
#define PORT 10086

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

//信号处理函数,所有线程也会调用该信号处理。
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
    printf("process id = %d", getpid());
    //忽略SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);

    threadpool<http_conn>* pool = new threadpool<http_conn>(8);
    
    //预先为每个可能的客户连接分配一个http_conn对象
    http_conn *users = new http_conn[MAX_FD];
    assert(users);
//  int user_count = 0;
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

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    int cond; //接收epoll_wait的返回值
    while(true)
    {
        printf("epoll_wait 阻塞住\n");
        cond = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        printf("epoll_wait返回成功\n");
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
                int connfd = accept(listenfd, (sockaddr*)&cli_addr, &cli_len);
                printf("有新的连接%d到来\n", connfd);
                if(connfd < 0)
                {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                //超过最大连接上限
                std::cout << "m_user_count = " << http_conn::m_user_count << "\n";
                if(http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                //初始化客户连接
                users[connfd].init(connfd, cli_addr);
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
                //pool->append(users + fd);
                ///* 
                //根据读的结果，决定是将任务添加到线程池，还是关闭连接
                if(users[fd].read())
                {
                    std::cout << "将任务加入到工作队列中\n";
                    pool->append(users + fd);
                }
                else
                {
                    users[fd].close_conn();
                }
                //*/
            }
            //可写事件
            else if(events[i].events & EPOLLOUT)
            {
                std::cout << "写事件触发\n";
                //根据写的结果，决定是否关闭连接
                if(!users[fd].write())
                {
                    users[fd].close_conn();
                }
                
            }
            else
            {
                std::cout << "未知响应\n";   
            }
        }
    }   
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}
