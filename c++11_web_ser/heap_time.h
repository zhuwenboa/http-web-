#ifndef HEAP_TIME_H
#define HEAP_TIME_H

#include<iostream>
#include<sys/types.h>
#include<fcntl.h>
#include<assert.h>
#include<unistd.h>
#include<string.h>
#include<netdb.h>
#include<time.h>
#include"http_conn.h"
#include<queue>
#include<vector>
#include<algorithm>
using std::exception;

class heap_timer; //前向声明

//定时器类
class heap_timer
{
public: 
    heap_timer() = default;
    heap_timer(int delay, const http_conn& a) : user_data(a)
    {
        expire = time(NULL) + delay; 
    }
    void init(const http_conn& a)
    {
        expire = time(NULL) + 5;
        user_data = a;
    }
    void retime()
    {
        expire = time(NULL) + 5;
    }
public:     
    time_t expire; //定时器生效的绝对时间
    void (*cb_func)(http_conn); //定时器的回调函数
    http_conn user_data; //用户数据

};

struct timer_compare
{
    bool operator() (const heap_timer& a, const heap_timer& b)
    {
        return a.expire > b.expire;
    }
};

//时间堆类
class time_heap
{
public: 
    /*默认构造函数*/
    time_heap();
    /*构造函数之二， 用已有数组来初始化堆*/
    time_heap(std::vector<heap_timer> init_array);
    ~time_heap();
    //添加目标定时器timer
    void add_timer(const heap_timer &timer);
    //删除目标定时器
    void del_timer(heap_timer timer);
    //心搏函数
    void tick();

    //开启定期唤醒
    void start_wakeup();

    int getTimerfd() {return timerfd;}
    bool TimeRead();
private: 
    //timerfd 用于定时到期唤醒epoll_wait函数去执行定时任务
    int timerfd; 
    std::priority_queue<heap_timer, std::vector<heap_timer>, timer_compare> heap;
};

#endif