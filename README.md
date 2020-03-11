# Web服务器 
有单线程和多线程两个版本

### 功能
- 高并发，高性能
- 支持GET请求
- 支持优雅关闭
- 时间堆定时器
- 服务器有读写BUFFER
- Fast_cgi服务器和web服务器进行交互

### web服务器中代码简要分析

#### web服务器主框架
- 采用Epoll+线程池的Reactor模型

#### 在web服务器中引入了Fast_cgi服务器
- Fast_cgi用进程池实现，主进程监听listenfd采用Round_Robin算法来将连接分配给子进程。
```cpp
                int i = sub_process_counter;
                do
                {
                    if(m_sub_process[i].m_pid != -1)
                        break;
                    i = (i + 1)% m_process_number;
                } while (i != sub_process_counter);
                
                if(m_sub_process[i].m_pid == -1)
                {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i + 1)%m_process_number
```
父进程通过管道将新连接发送给子进程。

- 每个子进程都有自己的epoll来监听该子进程上的所有套接字，子进程先将与父进程通信的管道加入到epoll中，确保和父进程正常通信。


- 引入该服务器是为了在处理动态页面是交给Fast_Cgi服务器进行处理,让web服务器只需要处理简单的静态页面，提高web服务器的效率。
- 服务器采用异步的方式来接收Fast_Cgi服务器返回的内容
```cpp
    //将connfd加入到epoll事件集中进行异步处理
    addfd(http_conn::m_epollfd, connfd, true)
```
将与Fast_CGi连接的套接字加入到epoll中，发送完消息直接返回，用Epoll来监听Fast_Cgi返回的内容在进行处理。
进行异步处理可以节省盲目等待的时间，提高效率。

- Epoll怎么确定该套接字是新的web连接还是fast_cgi返回的内容?
采用类中添加一个falg标志位，新的连接会将该标志位设为true，而我们的fast_cgi的套接字是false来解决。

#### 服务器采用时间堆定时器来处理超时事件
- 时间堆用优先队列来设计。
- 时间堆默认是大顶堆，而我们的定时器需要将其变为小顶堆，采用添加自定义的compare函数来将其改为小顶堆，每次我们只需要判断堆顶时间和当前时间是否超时即可。
- 每次该连接有活跃事件后，会重新更新定时器中该套接字的时间。

