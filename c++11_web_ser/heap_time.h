#ifndef HEAP_TIME_H
#define HEAP_TIME_H

#include<iostream>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<unistd.h>
#include<string.h>
#include<netdb.h>
#include<sys/epoll.h>
#include<pthread.h>
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

    heap_timer(int delay, const http_conn &a) : user_data(a)
    {
        expire = time(NULL) + delay; 
    }

    friend bool operator > (const heap_timer& a, const heap_timer& b)
    {
        return a.expire < b.expire; 
    }
    friend bool operator < (const heap_timer &a, const heap_timer &b)
    {
        return a.expire > b.expire;
    }
    void init(const http_conn &a)
    {
        expire = time(NULL) + 15;
        user_data = a;
    }
    void retime()
    {
        expire = time(NULL) + 15;
    }
public:     
    time_t expire; //定时器生效的绝对时间
    void (*cb_func)(http_conn); //定时器的回调函数
    http_conn user_data; //用户数据

};


//时间堆类
class time_heap
{
public: 
    /*默认构造函数*/
    time_heap() = default;
    /*构造函数之二， 用已有数组来初始化堆*/
    time_heap(std::vector<heap_timer> init_array)
    {
        for(auto i = init_array.begin(); i != init_array.end(); ++i)
        {
            heap.push(*i);
        }
    }

public: 
    //添加目标定时器timer
    void add_timer(const heap_timer &timer)
    {
        if(timer.expire <= 0)  
            return;
        heap.push(timer);
    }
    //删除目标定时器
    void del_timer(heap_timer timer)
    {
        if(timer.expire <= 0)
            return;
        /*
        仅将目标定时器的回调函数设置为空，既所谓的延迟销毁，
        这将节省真正删除定时器造成的开销，但这样做容易使得堆数组膨胀
        timer.cb_func = nullptr;
        */
       std::cout << "删除定时器函数运行\n";
    }

    //心搏函数
    void tick()
    {
        heap_timer tmp = heap.top();
        time_t cur = time(NULL);
        while(!heap.empty())
        {
            if(tmp.expire <= 0)
                break;
            //如果堆顶定时器没到期，则退出循环
            if(tmp.expire > cur)
                break;
            //否则就执行堆顶定时器的任务
            if(tmp.cb_func)
                tmp.cb_func(tmp.user_data);
            //将堆顶元素删除，同时生成新的定时器(array[0])
            heap.pop();
            tmp = heap.top();
        }
    }

private: 
    std::priority_queue<heap_timer> heap;
};

#endif