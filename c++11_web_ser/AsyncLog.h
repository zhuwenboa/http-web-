#ifndef ASYNCLOG_H
#define ASYNCLOG_H

#include<iostream>
#include<vector>
#include<string.h>
#include<string>
#include<queue>
#include<mutex>
#include<condition_variable>

class Log_queue;

class Log
{
public:  
    Log() : buffer_len(0) {}
    enum level
    {
        TRACE = 0,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
    };
    
    void add_log(const std::string&);
    void flush_log();
private: 
    std::vector<std::string> log_buffer;     
    int buffer_len;
    static const int MAX_LOG;
};

class Log_queue
{
public:   
    Log_queue() : 
                  backend_buffer_len(0), 
                  flag(true)
                  {}
    //删除拷贝构造和拷贝赋值函数
    Log_queue(const Log_queue&)  = delete;
    Log_queue operator = (const Log_queue&)  = delete;

    void append(std::vector<std::string>&);    
    static void run(void *arg);
    void work();
    
    bool OpenFile();
    
    void flush();

    void exit_log()
    {
        flag = false;
    }
private:  
    std::queue<std::vector<std::string>> work_queue;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    int backend_buffer_len;
    const int MAX_BACKEND_LEN = 20;
    bool flag;
    int Logfile_fd;
};

#endif