#include "AsyncLog.h"
#include<thread>
#include<unistd.h>
#include<sys/types.h>
#include<iostream>
#include<fcntl.h>

int Log::MAX_LOG = 100;

int log_fd = open("sys_log.txt", O_RDWR | O_CREAT, 0666);


Mutexlock Log_queue::mutex;
Condition Log_queue::cond(mutex);
std::queue<std::vector<std::string>> Log_queue::work_queue;
int Log_queue::MAX_BACKEND_LEN = 20;
int Log_queue::backend_buffer_len = 0;
bool Log_queue::flag = true;

void Log::add_log(const std::string& mes)
{
    //如果该工作线程的日志数大于MAX_LOG，则将其添加到工作线程进行处理
    if(buffer_len >= MAX_LOG)
    {
        Log_queue::append(log_buffer);
        buffer_len = 0;
        log_buffer.clear();
    }
    else
    {
        log_buffer.emplace_back(mes);
        ++buffer_len;    
    }
}

void Log_queue::append(std::vector<std::string> messages)
{
    mutex.lock();
    work_queue.emplace(messages);
    ++backend_buffer_len;
    if(backend_buffer_len >= MAX_BACKEND_LEN)
    {
        mutex.unlock();
        cond.notify();
    }
    else 
        mutex.unlock();
}

void Log_queue::work()
{
    while(flag)
    {
        mutex.lock();
        while(backend_buffer_len < MAX_BACKEND_LEN)
        {
            cond.wait();
        }
        std::queue<std::vector<std::string>> temp_queue;
        std::swap(temp_queue, work_queue);                   //用swap交换buffer，减少锁的粒度。
        backend_buffer_len = 0;
        mutex.unlock();

        //将数据写入磁盘
        for(int i = 0; i < temp_queue.size(); ++i)
        {
            for(auto a : temp_queue.front())
            {
                int ret = write(log_fd, a.c_str(), a.size());
                if(ret > 0)
                    continue;
                else
                {
                    //write error;       
                }
                temp_queue.pop();
            }
        }                
    }
}