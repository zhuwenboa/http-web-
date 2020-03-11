#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<iostream>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/uio.h>
#include<map>
class http_CGI;  //访问CGI服务器类声明

class http_conn
{
public:
    //文件名的最大长度
    static const int FILENAME_LEN = 200;
    //读缓冲区的大小
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;
    
    //HTTP请求方法，但我们仅支持get
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};

    //解析客户请求时，主状态机所处的状态
    enum CHECK_STATE 
    {
        CHECK_STATE_REQUESTLINE = 0,  //检查请求行
        CHECK_STATE_HEADER,  //检查请求头部
        CHECK_STATE_CONTENT //检查剩余字段
    };

    //服务器处理HTTP请求的可能结果
    enum HTTP_CODE 
    {
        NO_REQUEST = 0,  //请求不完整，需要读取客户数据
        GET_REQUEST, //获得了一个完整的客户请求
        BAD_REQUEST, //客户请求有语法错误
        NO_RESOURCE, //不能提供所请求的服务
        FORBIDDEN_REQUEST, //客户对资源没有足够的访问权限
        FILE_REQUEST,       //文件可以读取
        INTERNAL_ERROR, //服务器内部错误
        CLOSED_CONNECTION, //客户端已经关闭连接
        FAST_CGI           //需要访问CGI服务器进行解析
    };

    //行的读取状态
    enum LINE_STATUS
    {
        LINE_OK = 0,  //读取到一个完整的行
        LINE_BAD,    //行出错
        LINE_OPEN //行数据尚且不完整
    };
public:  
    http_conn () : flag(false), m_read_idx(0){}
    ~http_conn ()  {}
public:  
    //初始化新接受的连接
    void init(int sockfd, const sockaddr_in& addr);
    //关闭连接
    void close_conn(bool real_close = true);
    //处理客户请求
    void process();
    //非阻塞读操作()读取所有数据
    bool read();
    //非阻塞写操作
    bool write();

    friend class http_CGI;
    //判断是否为web的连接
    bool have_sockfd() {return flag;}
    //保存CGI连接套接字
    void cgi(int fd) {m_sockfd = fd;}
private:  
    //初始化连接
    void init();
    //解析http请求
    HTTP_CODE process_read();
    //填充http应答
    int process_write(HTTP_CODE ret);

    //下面这一组函数被process_read调用以分析http请求
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE pares_headers(char* text);
    HTTP_CODE pares_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS pares_line();

    //下面这一组函数被process_write调用以填充http应答
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    //所有socket上的事件都被注册到同一个epoll内核事件表中，所以epoll文件描述符设置为静态的
    static int m_epollfd;
    //统计用户数量
    static int m_user_count;

private:  
    //保存客户连接套接字
    int m_sockfd;
    sockaddr_in m_address;

    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    //标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
    int m_read_idx;
    //当前正在分析的字符在读缓冲区的位置
    int m_checked_idx;
    //当前正在解析的行起始位置
    int m_start_line;
    //写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    //写缓冲区中待发送的字节数
    int m_write_idx;
    
    //是否调用CGI服务器
    int m_cgi;

    //主状态机当前所处的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;
    
    //客户请求的目标文件的完整路径，其内容等于doc_root + m_url, doc_root是网站根目录
    char m_real_file[FILENAME_LEN];
    //客户请求的目标文件的文件名
    char* m_url;
    //HTTP协议版本号，此处 HTTP/1.1
    char* m_version;
    //主机名
    char* m_host;
    //HTTP请求的消息体长度
    int m_content_length;
    //HTTP请求是否要保持连接
    bool m_linger;

    //客户请求的目标文件被mmap到内存中的起始位置
    char* m_file_address;
    //目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct stat m_file_stat;
    //我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count标识被写内存块的数量
    struct iovec m_iv[2];
    int m_iv_count;

    //表明本对象中是否有连接
    bool flag;

};

class http_CGI
{
public:  
    //访问CGI服务器进行解析
    static bool fast_cgi(const char *file);
    //对请求的文件进行解析，判断是否为动态文件
    static bool cmp_file(const char *file);
    //处理CGI服务器返回的信息
    static bool deal_with_CGI(int fd);
};

#endif

