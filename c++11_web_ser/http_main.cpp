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
#include<functional>
#include "threadpool.h"
#include "http_conn.h"
#include "heap_time.h"
#include "AsyncLog.h"

#define MAX_FD 65536   
#define MAX_EVENT_NUMBER 10000
#define PORT 10086

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

extern Log_queue back_log_; //日志, 定义在AsyncLog.cpp中

void timer_func(http_conn maturity)
{
    //std::cout << "刷新日志函数执行\n";
}

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
    //忽略SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);
    //初始化线程池，启动工作线程
    threadpool<http_conn>* pool = new threadpool<http_conn>(6, 10000);

    //启动日志线程
    std::thread log_thread(Log_queue::run, &back_log_);

    //初始化定时器
    time_heap Timer; 

    //预先为每个可能的客户连接分配一个http_conn对象
    std::vector<http_conn> users;
    users.reserve(MAX_FD);
    
    //预先分配定时器类对象
    std::vector<heap_timer> time_;
    time_.reserve(MAX_FD);
    //主线程的工作日志
    Log main_log_;

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp = {1, 0}; //避免time_wait状态，断开连接时发送RST分节断开,主要用于测试服务器时使用
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
    addfd(epollfd, listenfd, false);

    //将定时器唤醒timerfd加入到epoll事件集中
    addfd(epollfd, Timer.getTimerfd(), false);
    Timer.start_wakeup();
    http_conn::m_epollfd = epollfd;

    int cond; //接收epoll_wait的返回值
    while(true)
    {
        cond = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
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
                while(true)
                {
                    int connfd = accept(listenfd, (sockaddr*)&cli_addr, &cli_len);
                    if(connfd < 0)
                    {
                        if(errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        printf("errno is: %d\n", errno);
                        exit(1);
                    }
                    //超过最大连接上限
                    if(http_conn::m_user_count >= MAX_FD)
                    {
                        show_error(connfd, "Internal server busy");
                        break;
                    }
                    //初始化客户连接
                    users[connfd].init(connfd, cli_addr);

                    //将新到来的连接加入定时器中
                    time_[connfd].init(users[connfd]);
                    time_[connfd].cb_func = timer_func;
                    Timer.add_timer(time_[connfd]);
                }
            }
            //处理定时任务
            else if(fd == Timer.getTimerfd())
            {
                if(Timer.TimeRead())
                {
                    Timer.tick();
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
                //判断是否为浏览器发来的请求
                if(users[fd].have_sockfd())
                {
                    //根据读的结果，决定是将任务添加到线程池，还是关闭连接
                    if(users[fd].read())
                    {
                        pool->append(&users[fd]);
                        //更新定时器
                        time_[fd].retime();
                    }
                    else
                    {
                        users[fd].close_conn();
                        Timer.del_timer(time_[fd]);
                    }
                }
                //CGI返回的内容
                else 
                {
                    users[fd].cgi(fd);
                    if(users[fd].read())
                    {
                        //printf("CGI发来请求\n");
                        pool->append(&users[fd]);
                    }
                    users[fd].close_conn();
                    Timer.del_timer(time_[fd]);
                }   
            }   
            //可写事件
            else if(events[i].events & EPOLLOUT)
            {
                //根据写的结果，决定是否关闭连接
                if(!users[fd].write())
                {
                    Timer.del_timer(time_[fd]);
                    users[fd].close_conn();
                }
            }
            else
            {
                Timer.del_timer(time_[fd]);
                users[fd].close_conn();
                std::cout << "未知响应\n";

            }
        }
    }   
    close(epollfd);
    close(listenfd);
    delete pool;
    //终止日志线程
    back_log_.exit_log();
    log_thread.join();
    return 0;
}
