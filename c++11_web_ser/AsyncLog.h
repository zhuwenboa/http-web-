#ifndef ASYNCLOG_H
#define ASYNCLOG_H
#include<vector>
#include<string.h>
#include<string>
#include<queue>
#include "Mutexlock.h"

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

private: 

    std::vector<std::string> log_buffer;     
    int buffer_len;
    static int MAX_LOG;
};

class Log_queue
{
public:   
    Log_queue() {}
    //删除拷贝构造和拷贝赋值函数
    Log_queue(const Log_queue&)  = delete;
    Log_queue operator = (const Log_queue&)  = delete;

    static void append(std::vector<std::string>);    
    static void work();
    static void exit_log()
    {
        flag = false;
    }
private:  
    static std::queue<std::vector<std::string>> work_queue;
    static Mutexlock mutex;
    static Condition cond;
    static int backend_buffer_len;
    static int MAX_BACKEND_LEN;
    static bool flag;
};

#endif