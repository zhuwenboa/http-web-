#include"heap_time.h"
#include<sys/timerfd.h>

int create_timerfd()
{
    int timerfd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
    assert(timerfd != -1);
    return timerfd;
}

time_heap::time_heap()
    : timerfd(create_timerfd())
{

}

time_heap::~time_heap()
{
    close(timerfd);
}

time_heap::time_heap(std::vector<heap_timer> init_array)
    :timerfd(create_timerfd())
{   
    for(auto i = init_array.begin(); i != init_array.end(); ++i)
    {
        heap.emplace(*i);
    }
}

void time_heap::add_timer(const heap_timer &timer)
{
    if(timer.expire <= 0)  
        return;
    heap.emplace(timer);
}

void time_heap::del_timer(heap_timer timer)
{
    if(timer.expire <= 0)
        return;
    /*
    仅将目标定时器的回调函数设置为空，既所谓的延迟销毁，
    这将节省真正删除定时器造成的开销，但这样做容易使得堆数组膨胀
    */
    timer.cb_func = nullptr;
}

void time_heap::tick()
{
    while(!heap.empty())
    {
        heap_timer tmp = heap.top();
        time_t cur = time(NULL);
        //如果堆顶定时器没到期，则退出循环
        if(tmp.expire > cur)
            break;
        //执行堆顶定时器的任务
        if(tmp.cb_func)
            tmp.cb_func(tmp.user_data);
        //将堆顶元素删除，同时生成新的定时器(array[0])
        heap.pop();
    }
}

void time_heap::start_wakeup()
{
    struct itimerspec new_time;
    struct timespec now;
    int ret = clock_gettime(CLOCK_REALTIME, &now);
    assert(ret != -1);

    //第一次到期时间
    new_time.it_value.tv_sec = 5;
    new_time.it_value.tv_nsec = now.tv_nsec;

    //之后每次到期的时间间隔
    new_time.it_interval.tv_sec = 5;
    new_time.it_interval.tv_nsec = 0;
    //启动
    ret = timerfd_settime(timerfd, 0, &new_time, NULL);
    assert(ret != -1);
}

bool time_heap::TimeRead()
{
    uint64_t one;
    ssize_t size = read(timerfd, &one, sizeof(one));
    if(size == sizeof(one))
        return true;
    return false;
}