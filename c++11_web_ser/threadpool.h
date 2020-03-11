//半同步半反应堆模式
//该模式由主线程处理读写请求，将封装好的任务类添加到任务队列中，然后由工作线程取出任务进行处理
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<iostream>
#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include<vector>
#include<thread>
#include<mutex>
#include<condition_variable>
//线程池类,将它定义为模板类是为了代码复用。模板参数T是任务类
template<typename T>
class threadpool
{
public:  
    /*参数thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理请求的数量 */
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    //往请求队列中添加任务
    bool append(T* request);

    //工作线程运行函数
    static void* worker(void* arg);
    void run();

private:  
    int m_thread_number; //线程池中的线程数
    int m_max_requests; //请求队列中允许的最大请求数
    //pthread_t* m_threads; //描述线程池的数组，其大小为m_thread_number
    std::list<T*>m_workqueue; //请求队列
    std::vector<std::thread> p; //vector存储线程池
    mutable std::mutex m;                          //锁   
    mutable std::condition_variable cond;    //条件变量
    bool m_stop; //是否结束线程
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests)
        : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false)
{
    if((thread_number <= 0) || (max_requests <= 0) )
        throw std::exception();
    
    //创建线程池
    p.reserve(thread_number); //先一次扩容
    for(int i = 0; i < thread_number; ++i)
    {
        p.push_back(std::thread(worker, this));
        printf("create %d process \n", i);
        p[i].detach(); //线程分离
    }             
}
            
template<typename T>  
threadpool<T>::~threadpool()
{
    //delete [] m_threads;
    m_stop = true;
}

//将任务添加到任务队列中
template<typename T>  
bool threadpool<T>::append(T* request)
{
    //操作工作队列是一定要加锁，因为它被所有线程共享
    m.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    std::cout << "任务加入成功\n";
    std::cout << "任务队列中任务数目： " <<m_workqueue.size() << "\n";
    m.unlock();
    cond.notify_one();   //唤醒一个线程 
    
    return true;
}

template<typename T> 
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template<typename T>  
void threadpool<T>::run()
{
    while(!m_stop)
    {
        std::unique_lock<std::mutex> lk(m);
        while(m_workqueue.empty())
        {
            cond.wait(lk); //传递一个锁和一个lambda函数表达式(作为等待的条件)
        }
        T *quest = m_workqueue.front();
        m_workqueue.pop_front();
        lk.unlock();
        if(!quest)
            continue;
        quest->process();
    }
}
#endif