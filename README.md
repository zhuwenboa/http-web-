# Web服务器 
有单线程和多线程两个版本

### 功能
- 高并发，高性能
- 支持GET请求
- 支持优雅关闭
- 时间堆定时器
- 服务器有读写BUFFER
- 保活机制 keepalive
- 状态机解析HTTP协议
- Fast_cgi服务器和web服务器进行交互

### web服务器中代码简要分析

#### web服务器主框架
- 主线程epoll+线程池处理任务的Reactor事件驱动模型
- Epoll采用ET模式


#### 在web服务器中引入了Fast_cgi服务器
- https://zh.wikipedia.org/wiki/FastCGI 维基百科
- Fast_cgi用进程池实现，主进程监听listenfd采用Round_Robin算法来将连接分配给子进程。
父进程通过管道将新连接发送给子进程。

- 每个子进程都有自己的epoll来监听该子进程上的套接字，子进程先将与父进程通信的管道加入到epoll中，确保和父进程正常通信。
- 引入该服务器是为了在处理动态页面是交给Fast_Cgi服务器进行处理,让web服务器只需要处理简单的静态页面，提高web服务器的效率。
###### 服务器采用异步的方式来接收Fast_Cgi服务器返回的内容
```cpp
    //将connfd加入到epoll事件集中进行异步处理
    addfd(http_conn::m_epollfd, connfd, true)
```
将与Fast_CGi连接的套接字加入到epoll中，发送完消息直接返回，用Epoll来监听Fast_Cgi返回的内容在进行处理。
进行异步处理可以节省盲目等待的时间，提高效率。

- Epoll怎么确定该套接字是新的web连接还是fast_cgi返回的内容?
采用类中添加一个flag标志位，新的连接会将该标志位设为true，而我们的fast_cgi的套接字是false来解决。

#### mmap
    当分析完http请求后，解析出文件名，将需要访问的文件用mmap建立内存映射来直接读取。减少访问磁盘的消耗，提高效率。

#### 服务器采用时间堆定时器来处理超时事件
- 时间堆用优先队列来设计。
- 时间堆默认是大顶堆，而我们的定时器需要将其变为小顶堆，采用添加自定义的compare函数来将其改为小顶堆，每次我们只需要判断堆顶时间和当前时间是否超时即可。
- 每次该连接有活跃事件后，会重新更新定时器中该套接字的时间。

时间堆中有timerfd, 是linux中用于设置定时唤醒的套接字，可以加入到多路复用中。
采用timerfd来定时通知IO线程执行定时任务。

- 时间堆的删除
因为priority_queue只支持pop的操作，所以我们想要删除定时器，我采用将回调函数指针设为NULL，延时删除的方法(时间上更加高效，空间消耗增加)。

#### 日志
- 日志采用前后端分离的异步日志
- 工作线程有自己的一个缓冲buffer，每个工作线程先向工作buffer中写，当写到一定数量然后加入的日志队列中，有专门的日志线程读取并写入磁盘。
- 采用该方法可以避免每个线程都需要往磁盘中写，增加IO时间开销，由专门的IO线程去写入磁盘。
```cpp
void Log_queue::append(std::vector<std::string>& messages);
void Log_queue::work();
```

- 在日志线程中，当缓冲区到达该写入文件的临界点时，我们创建一个临时对象，用std::swap交换，可以减小锁的粒度，而不用等待将所有数据写到文件中再解锁。
```cpp
        mutex.lock();
        while(work_queue.empty())
        {
            cond.wait();
        }
        std::queue<std::vector<std::string>> temp_queue;
        std::swap(temp_queue, work_queue);                   //用swap交换buffer，减少锁的粒度。
        backend_buffer_len = 0;
        mutex.unlock();
```


### 如何安装使用
- 下载源码：然后进入到c++11_web_ser 执行make
- ./webserver 就可以执行服务器
- 浏览器如何访问： http://127.0.0.1:10086/index.html

- fast_cgi 安装
- g++ my_serve.cpp -o fast_cgi
- ./fast_cgi 就可以执行fast_cgi服务器