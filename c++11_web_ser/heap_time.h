#ifndef HEAP_TIME
#define HEAP_TIME

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

using std::exception;

class heap_timer; //前向声明
/*
//绑定http_conn类和定时器
struct client_data
{
    http_conn obj;     
    heap_timer* timer;
};
*/
//定时器类
class heap_timer
{
public: 
    heap_timer() = default;
    heap_timer(int delay, const http_conn &a) : user_data(a)
    {
        expire = time(NULL) + delay; 
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
    static void (*cb_func)(http_conn); //定时器的回调函数
    http_conn user_data; //用户数据

};

//时间堆类
class time_heap
{
public: 
    /*构造函数之一，初始化一个大小为cap的空对*/
    time_heap(int cap) : capacity(cap), cur_size(0)
    {
        array = new heap_timer* [capacity]; //创建堆数组
        if(!array)
            throw std::exception();
        for(int i = 0; i < capacity; ++i)
            array[i] = nullptr;   
    }
    /*构造函数之二， 用已有数组来初始化堆*/
    time_heap(heap_timer** init_array, int size, int cap) : cur_size(size), capacity(cap)
    {
        if(capacity < size)
            throw std::exception();
        array = new heap_timer*[capacity]; //创建堆数组
        if(!array)
            throw std::exception();
        if(size)
        {
            for(int i = 0; i < capacity; ++i)
                array[i] = init_array[i];
            for(int i = (cur_size -1) / 2; i >=0; --i)
                //对数组中的第[(cur_size - 1) /2 ] ~ 0个元素执行下滤操作
                percolate_down(i);
        }
    }
    //销毁时间堆
    ~time_heap()
    {
        for(int i = 0; i < cur_size; ++i)
        {
            delete array[i];
        }
        delete []array;
    }

public: 
    //添加目标定时器timer
    void add_timer(heap_timer* timer)
    {
        if(!timer)  
            return;
        if(cur_size >= capacity) //如果数组容量不够，则将其扩大一倍
            resize();
        //新插入一个元素，当前堆大小加1，hole是新建空穴的位置
        int hole = cur_size++;
        int parent = 0;
        //对从空穴到根节点的路径上的所有节点执行上滤操作
        for(; hole > 0; --hole)
        {
            parent = (hole -1) / 2;
            if(array[parent]->expire > timer->expire)
            {
                array[hole]->expire = array[parent]->expire;
                continue;
            }
            array[hole]->expire = timer->expire;
            break;
        }
    }
    //删除目标定时器
    void del_timer(heap_timer* timer)
    {
        if(!timer)
            return;
        /*
        仅将目标定时器的回调函数设置为空，既所谓的延迟销毁，
        这将节省真正删除定时器造成的开销，但这样做容易使得堆数组膨胀
        */
        timer->cb_func = nullptr;
    }

    //获得堆顶部的定时器
    heap_timer* top() const
    {   
        if(empty())
            return nullptr;
        return array[0];
    }

    //删除堆顶部的定时器
    void pop_timer()
    {
        if(empty())
            return;
        if(array[0])
        {
            delete array[0];
            //将原来的堆顶元素替换为堆数组中最后一个元素
            array[0] = array[--cur_size];
            percolate_down(0); //堆新的堆顶元素执行下滤操作
        }
    }

    //心搏函数
    void tick()
    {
        heap_timer* tmp = array[0];
        time_t cur = time(NULL);
        while(!empty())
        {
            if(!tmp)
                break;
            //如果堆顶定时器没到期，则退出循环
            if(tmp->expire > cur)
                break;
            //否则就执行堆顶定时器的任务
            if(array[0]->cb_func)
                array[0]->cb_func(array[0]->user_data);
            //将堆顶元素删除，同时生成新的定时器(array[0])
            pop_timer();
            tmp = array[0];
        }
    }

    //堆数组是否为空
    bool empty() const {return cur_size == 0;}
private: 
    //最小堆的下滤操作，它确保数组中以第hole个节点为根的子树拥有最小堆性质
    void percolate_down(int hole)
    {
        heap_timer* temp = array[hole];
        int child = 0;
        for(; ((hole * 2 + 1) <= (cur_size -1) ); hole = child)
        {
            child = hole * 2 + 1;
            if(child < cur_size -1 && array[child]->expire > array[child + 1]->expire)
                ++child;
            if(temp->expire < array[child]->expire)
                break;
            array[hole]->expire = array[child]->expire;
        }
        array[hole]->expire = temp->expire; 
    }

    //将堆数组容量扩大一倍
    void resize()
    {
        heap_timer** temp = new heap_timer* [capacity *2]; //扩容
        for(int i = 0; i < 2*capacity; ++i)
            temp[i] = nullptr;
        capacity = 2 *capacity;
        for(int i =0; i < capacity; ++i)
            temp[i] = array[i];
        delete []array;
        array = temp;
    }

private: 
    heap_timer** array; //堆数组
    int capacity; //堆数组容量
    int cur_size; //堆数组当前包含元素的个数

};

#endif