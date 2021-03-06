#include "http_conn.h"
//定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
//网站的根目录
const char* doc_root = "/home/xuexi/unix-/linux-高性能/15/web";


//将套接字设置成非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int ret = fcntl(fd, F_SETFL, old_option | O_NONBLOCK);
    if(ret < 0)
    {   
        std::cout << "fcntl erro  line is " << __LINE__ << std::endl;
    }
    return old_option;
}

void addfd(int epollfd, int fd, bool oneshot)
{
    setnonblocking(fd);
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(oneshot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}
//关闭连接
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//更换epollfd的事件集
void modfd(int epollfd, int fd, int ev)
{    
    epoll_event event;
    event.data.fd = fd;
    event.events =  ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    int ret = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = 0;


void  http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; //关闭一个连接，将客户数量减一
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    flag = true;
    m_sockfd = sockfd;
    m_address = addr;
    //int reuse = 1;
    //用该函数避免time_wait状态
    //setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); //设置地址重用
    addfd(m_epollfd, sockfd, true);
    m_user_count++; 

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    log_ = NULL;
    memset(m_read_buf, 0, sizeof(m_read_buf));
    memset(m_write_buf, 0, sizeof(m_write_buf));
    memset(m_real_file, 0, sizeof(m_real_file));
}

//分析http每一行
http_conn::LINE_STATUS http_conn::pares_line()
{
    char temp;
    /*
    m_checked_ide指向的m_read_buf(应用程序读缓冲区)中，当前正在分析的字节，m_read_index指向m_read_buf中客户数据的
    尾部的下一个字节。m_read_buf中第0~m_checked_idx字节都已经分析完毕，第m_checked_idx到m_read_index -1个字节由
    下面的循环进行分析
    */
    for(; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        //获得当前要分析的字节
        temp = m_read_buf[m_checked_idx];
        
        //如果当前的字节是回车符，则说明可能读取到一个完整的行
        if(temp == '\r')
        {
            /*如果\r字符碰巧是目前buffer中的最后一个已经被读入的客户数据，
            那么这次分析没有读到一个完整的行，返回LINE_OPEN以表示还需要继续读取客户数据才能进一步分析*/
            if((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            //如果下一个字符是'\n',则说明我们成功读取到一个完整的行
            else if(m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //说明客户发送的HTTP请求存在语法问题
            return LINE_BAD;            
        }
        //如果当前的字节是'\n',既换行符，则也说明可能读取到一个完整的行
        else if(temp == '\n')
        {
            if( (m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')  )
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }    
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    while(true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        else if(bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

//解析http请求行，获得请求方法，目标URL,以及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t"); //strpbrk函数在字符串中找到最先含有搜索字符串的位置并返回，若找不到返回空指针。
    //如果请求行中没有空白字符或'\t'字符，则HTTP请求必有问题
    if(!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    
    //method存储请求的方法
    char* method = text;
    //strcasecmp函数，忽略大小写比较字符串
    if(strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }
    //size_t strspn(const char *s, const char *accept);返回字符串s开头连续包含字符串accept内的数目
    m_url += strspn(m_url, " \t");
    
    
    m_version = strpbrk(m_url, " \t");
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    //分析完请求行后，将状态改为该分析头部状态
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::pares_headers(char* text)
{
    //遇到空行，表示头部字段解析完毕
    if(text[0] == '\0')
    {
        //如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT状态
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    //处理Connection头部字段
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");

        //Connection: keep-alive 持久化连接，既TCP连接默认不关闭，可以被多个请求复用，不用声明。
        //Connection: close,明确要求服务器关闭TCP连接
        if(strcasecmp(text, "keep-alive") == 0)
        {
            //std::cout << "keep-alive\n";
            m_linger = true;
        }
    }
    //处理Content-Length头部字段
    /*
    一个TCP连接现在可以传送多个回应，势必要有一种机制，区分数据包属于哪一个回应。这就是Content-length字段的声明，声明本次回应的数据长度
    Content-length: 3495
    上面的代码告诉浏览器，本次回应的长度是3495个字节，后面的字节就属于下一个回应了。
    */  
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    //处理Host头部字段 Host: www.baidu.com Host之后跟的是域名
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        //printf("=======! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

//没有真正解析HTTP请求消息体，只是判断它是否被完整地读入了
http_conn::HTTP_CODE http_conn::pares_content(char *text)
{
    if(m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//主状态机。
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    while( ( (m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) ) 
                || ( (line_status = pares_line()) == LINE_OK) )
    {
        //获取分析请求的一行内容
        text = get_line();
        //将读取的行数加1（相当于+m_checked_idx）
        m_start_line = m_checked_idx;

        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = pares_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT: 
            {
                ret = pares_content(text);
                if(ret == GET_REQUEST)
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

/*
当得到一个完整的、正确的HTTP请求时，我们就分析目标文件的属性。如果目标文件存在，对所有用户可读，
且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
*/
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root); //将网站根目录拷贝到m_real_file中
    int len = strlen(doc_root);
    //将解析出的网站名复制到网站根目录后
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if(http_CGI::cmp_file(m_real_file))
        return FAST_CGI;
    /*
    定义函数:    int stat(const char *file_name, struct stat *buf);
    函数说明:    通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
    返回值:      执行成功则返回0，失败返回-1，错误代码存于errno
    */
    if(stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    //判断文件权限
    if(! (m_file_stat.st_mode & S_IROTH) ) //S_IROTH 其它用户可读取权限
    {
        return FORBIDDEN_REQUEST;
    }

    if(S_ISDIR(m_file_stat.st_mode)) //判断是否为目录
    {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    if(fd < 0)
    {
        perror("open file erro");
    }
    //用mmap将文件映射到内存中，用mmap将文件映射到内存中，减少拷贝次数，直接进行发送
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

//对内存映射区执行munmap操作
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//写HTTP响应
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send;
    if(m_iv_count == 1)
        bytes_to_send = m_write_idx;
    else 
        bytes_to_send = m_write_idx + m_iv[1].iov_len;
    //如果发送缓冲区中无数据，则更新epoll选项，加入EPOLLIN事件
    if(bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp < 0)
        {
            /*
            如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件。
            虽然在此期间，服务器无法立即接收到同一个客户的下一个请求，但这可以保证连接的完整性
            */
            if(errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }  
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        //响应发送完成
        if(bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if(m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;        
            }            
        }
        if(bytes_have_send >= m_write_idx)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else 
        {
            m_iv[0].iov_len -= bytes_have_send;
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
        }
    }
}

//往写缓冲区写入待发送的数据
bool http_conn::add_response(const char *format, ...)
{
    //写缓冲区待发送的数据大于写缓冲区，返回错误
    if(m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    //处理可变参数
    va_list arg_list; //指向参数的地址
    va_start(arg_list, format); //arg_list指向format的地址,函数压栈是从右向左压栈
   
    /*
    vsnprintf函数
    头文件：#include  <stdarg.h>
    函数原型：int vsnprintf(char *str, size_t size, const char *format, va_list ap);
    函数说明：将可变参数格式化输出到一个字符数组
    参数：
    str输出到的数组，size指定大小，防止越界，format格式化参数，ap可变参数列表函数用法
    */
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 -m_write_idx, format, arg_list);

    if(len >= (WRITE_BUFFER_SIZE - 1- m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) 
           && add_linger() 
           && add_blank_line();
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "index/html");
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::wirte_log(const std::string& mes, Log& log)
{
    log.add_log(std::move(mes));
    return true;
}

//根据服务器处理HTTP请求的结果，决定返回给客户端的内容
int http_conn::process_write(http_conn::HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
                return -1;
            break;
        }
        case BAD_REQUEST:  
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form))
                return -1;
            break;
        }
        case NO_RESOURCE:  
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form))
                return -1;
            break;
        }
        case FORBIDDEN_REQUEST:  
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_404_form))
                return -1;
            break;
        }
        //文件可以读取
        case FILE_REQUEST:  
        {
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;  
                m_iv[0].iov_len = m_write_idx; 
                m_iv[1].iov_base = m_file_address;   //请求文件的mmap映射对象
                m_iv[1].iov_len = m_file_stat.st_size;  //文件大小
                m_iv_count = 2;
                return 1;
            }
            else
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                {
                    return -1;
                }
            }
        }
        case FAST_CGI:
        {
            //访问CGI服务器进行文件解析
            if(http_CGI::fast_cgi(m_real_file))
                return 2;
            return -1;
        }
        default:  
            return -1;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return 1;
}

//由于线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process(Log* log)
{
    log_ = log;
    //处理CGI返回的内容
    if(!flag)
    {
        http_CGI::deal_with_CGI(m_sockfd);\
        return;
    }
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    int write_ret = process_write(read_ret);

    if(write_ret == -1)
    {
        close_conn();
        return;
    }
    if(write_ret == 1)
    {
        modfd(m_epollfd, m_sockfd, EPOLLOUT);
        return;
    }
    //需要等待CGI异步处理完成，继续监控可读事件
    if(write_ret == 2)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    log_ = NULL;
}


bool http_CGI::cmp_file(const char *file)
{
    int len = strlen(file);
    int flag;
    //取出文件的后缀文件格式
    for(int i = len - 1; i >= 0; --i)
    {
        if(file[i] == '.')
        {
            flag = i;
            break;
        }
    }
    const char *temp = &file[flag + 1];
    char buf[128] = {0};
    if(strcmp(temp, "php") == 0)
        return true;
    return false;
}

//异步CGI 
bool http_CGI::fast_cgi(const char *file)
{
    char writebuf[1024] = {0};
    char *ip = "127.0.0.1";
    int port = 8888;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    int connfd = socket(AF_INET, SOCK_STREAM, 0);
    if(connfd < 0)
        return false;
    setnonblocking(connfd);
    //将fd加入到epoll事件集中进行异步处理
    addfd(http_conn::m_epollfd, connfd, true);
    
    sprintf(writebuf, "%d:%s", connfd, file);
    //连接CGI服务器
    int ret = connect(connfd, (sockaddr*)&addr, sizeof(addr));
    if(ret < 0)
        return false;

    //给CGI服务器发送需要解析的文件
    ret = send(connfd, writebuf, sizeof(writebuf), 0);
    //如果发送失败，则将connfd从epoll中删除
    if(ret < 0)
    {
        removefd(http_conn::m_epollfd, connfd);
        return false; 
    }
    return true;
}

bool http_CGI::deal_with_CGI(int fd)
{
    //处理CGI发来的内容
    /*
        ...
    */
    return true;
}

