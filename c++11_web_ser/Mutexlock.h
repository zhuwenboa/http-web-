#include<iostream>
#include<thread>
#include<mutex>
#include<pthread.h>
#include<unistd.h>
#include<assert.h>


class Mutexlock
{
public:  
    Mutexlock() : holder_(0) 
    {
        pthread_mutex_init(&mutex_, NULL);
    }
    ~Mutexlock()
    {
        pthread_mutex_destroy(&mutex_);
    }

    bool isLockedByThisThread()
    {
        return holder_ == pthread_self();
    }

    void assertLocked()
    {
        assert(isLockedByThisThread());        
    }

    void lock()
    {
        pthread_mutex_lock(&mutex_);
        holder_ = pthread_self();
    }

    void unlock()
    {
        holder_ = 0;
        pthread_mutex_unlock(&mutex_);
    }

    pthread_mutex_t* getPthreadMutex()
    {
        return &mutex_;
    }
private:  
    Mutexlock& operator = (const Mutexlock&);
    pthread_mutex_t mutex_;
    pid_t holder_;

};

class MutexlockGuard
{
public:  
    explicit MutexlockGuard(Mutexlock& mutex) : mutex_(mutex) 
    {
        mutex_.lock();        
    }
    ~MutexlockGuard()
    {
        mutex_.unlock();
    }
private:  
    Mutexlock mutex_;
};
//定义这一行就是为了防止下列doit函数出现
#define MutexlockGuard(x) static_assert(false, "missing mutex guard var name")
/*
void doit()
{
    Mutexlock mtuex;
    MutexlockGuard(mutex); //这是一个右值，定义之后就立刻销毁了，所以不会锁住临界区造成恶性竞争
    正确写法
    MutexlockGuard lock(mutex);
}
*/

class Condition
{
public:  
    explicit Condition(Mutexlock &mutex) : mutex_(mutex)
    {
        pthread_cond_init(&pcond_, NULL);
    }
    ~Condition()
    {
        pthread_cond_destroy(&pcond_);
    }

    void wait()
    {
        pthread_cond_wait(&pcond_, mutex_.getPthreadMutex());
    }
    void notify()
    {
        pthread_cond_signal(&pcond_);
    }
    void notifyall()
    {
        pthread_cond_broadcast(&pcond_);
    }
private:  
    Mutexlock mutex_;
    pthread_cond_t pcond_;
};

//如果一个类中要包含Mutexlock和Condition类，则要先构造Mutexlock。
