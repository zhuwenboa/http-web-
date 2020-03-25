#include "AsyncLog.h"
#include<thread>
#include<unistd.h>
#include<sys/types.h>
#include<iostream>
#include<fcntl.h>

class Log_queue back_log_;

int Log::MAX_LOG = 100;


void Log::add_log(const std::string& mes)
{
    //如果该工作线程的日志数大于MAX_LOG，则将其添加到工作线程进行处理
    if(buffer_len >= MAX_LOG)
    {
        back_log_.append(log_buffer);
        buffer_len = 0;
        log_buffer.clear();
    }
    else
    {
        log_buffer.emplace_back(mes);
        ++buffer_len;    
    }
}


void Log_queue::append(std::vector<std::string>& messages)
{
    mutex_.lock();
    work_queue.emplace(messages);
    ++backend_buffer_len;
    if(backend_buffer_len >= MAX_BACKEND_LEN)
    {
        mutex_.unlock();
        cond_.notify_one();
    }
    else 
        mutex_.unlock();
}

//单独开辟线程调用此函数
void Log_queue::run(void *arg)
{
    Log_queue* log = static_cast<Log_queue*>(arg);
    if(log->OpenFile())
        log->work();
}

//工作函数
void Log_queue::work()
{
    while(flag)
    {
        std::unique_lock<std::mutex> lk(mutex_);
        while(backend_buffer_len < MAX_BACKEND_LEN)
        {
            cond_.wait(lk);
        }
        std::queue<std::vector<std::string>> temp_queue;
        std::swap(temp_queue, work_queue);                   //用swap交换buffer，减少锁的粒度。
        backend_buffer_len = 0;
        mutex_.unlock();

        //将数据写入磁盘
        for(int i = 0; i < temp_queue.size(); ++i)
        {
            for(auto a : temp_queue.front())
            {
                int ret = write(Logfile_fd, a.c_str(), a.size());
                if(ret > 0)
                {
                    std::cout << "写入文件成功\n";
                    continue;
                }
                else
                {
                    //write error;       
                }
                temp_queue.pop();
            }
        }                
    }
    close(Logfile_fd);
}

bool Log_queue::OpenFile()
{
    Logfile_fd = open("log.txt", O_CREAT | O_RDWR, 0666);
    if(Logfile_fd < 0)
    {
        return false;
    }
    return true;
}