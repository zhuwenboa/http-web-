#ifndef EPOLL_H
#define EPOLL_H
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<iostream>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/uio.h>
#include<vector>

class Epoll 
{
public:  
    Epoll() : epollfd(epoll_create(5))
    {
        events.reserve(MAX_EVENT_NUMBER);
    }
    int add(int fd);
    int del(int fd);
    int mod(int fd, int flags);


private:  
    std::vector<epoll_event> events;
    int epollfd;
    const int MAX_EVENT_NUMBER = 10000;
};


#endif