#include"processpool.h"
#include<string.h>
#define port 8888

class my_serve
{
public:  
    my_serve() {}
    ~my_serve() {}
    void init(int epollfd, int connfd, const struct sockaddr_in& client_addr)
    {
        m_epollfd = epollfd;
        m_sockfd = connfd;
        m_address = client_addr;
        memset(m_readbuf, 0, sizeof(m_readbuf));
        memset(m_writebuf, 0, sizeof(m_writebuf));
    }
    //进程池中的回调函数(服务器处理业务函数)
    void process()
    {
        int ret = 0;
        memset(m_readbuf, 0, BUFFER_SIZE);
        int reset = BUFFER_SIZE;
        while(true)
        {
            ret = recv(m_sockfd, m_readbuf, reset, 0);
            std::cout << "recv data: " << m_readbuf << std::endl;
            if(reset < 0)
            {
                std::cout << "发送数据超出缓冲区大小\n";
                return;
            }
            if(ret < 0)
            {
                //因为是ET工作模式，所以要一次将数据处理完毕
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                else
                {
                    //客户端出现错误,将其从epoll事件集中移出
                    std::cout << "客户端出现错误，我们将其关闭\n";
                    removefd(m_epollfd, m_sockfd);
                    return;
                }
            }
            //对端关闭连接
            if(ret == 0)
            {
                std::cout << "客户端: " << m_sockfd << " 关闭连接\n";
                removefd(m_epollfd, m_sockfd);
                return;
            }
            reset -= ret;
        }
        int buffer_flag;
        for(int i = 0; i < BUFFER_SIZE; ++i)
        {
            if(m_readbuf[i] == ':')
            {
                buffer_flag = i; 
                break;   
            }
        }
        char temp[buffer_flag];
        memset(temp, '0', buffer_flag);
        temp[0] = m_readbuf[0];
        //strncpy(temp, m_readbuf, buffer_flag);
        printf("temp = %s\n", temp);
        char *str = "i am fast_cgi's message";
        sprintf(m_writebuf, "%s#%s", temp, str);
        std::cout << "writebuf: " << m_writebuf << "\n";
        ret = send(m_sockfd, m_writebuf, BUFFER_SIZE, 0);
        if(ret < 0)
        {
            printf("send erro\n");
            return;
        }
        printf("发送成功\n");
    }

private:
    //缓冲区大小
    static const int BUFFER_SIZE = 1024;
    static int m_epollfd;
    int m_sockfd;
    struct sockaddr_in m_address;
    //读写缓存
    char m_readbuf[BUFFER_SIZE];
    char m_writebuf[BUFFER_SIZE];
};

int my_serve::m_epollfd = -1;

int main(int argc, char *argv[])
{
    int listenfd;
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0)
    {
        std::cout << "socket failed\n";
        return -1;
    }
    //设置为非阻塞
    setnonblocking(listenfd);
    int ret = bind(listenfd, (sockaddr*)&serv_addr, sizeof(serv_addr));
    if(ret < 0)
    {
        std::cout << "bind is error\n";
        return -1;
    }
    ret = listen(listenfd, 5);
    if(ret < 0)
    {
        std::cout << "listen failed\n";
        return -1;
    }
    //创建关于my_serve的进程池
    processpool<my_serve> *pool = processpool<my_serve>::create(listenfd);
    if(pool)
    {
        //运行
        pool->run();
        delete pool;
    }
    close(listenfd);
    return 0;
}