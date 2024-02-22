#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"
class http_conn{
public:
    /*设置读取文件的名称m_rea_file大小*/
    static const int FILENAME_LEN = 200;
    /*设置读缓冲区m_read_buf大小*/
    static const int READ_BUFFER_SIZE = 2048;
    /*设置写缓冲区m_write_buf大小*/
    static const int WRITE_BUFFER_SIZE = 1024;

    /*报文的请求方法，本项目只用到GET和Post*/
    enum METHOD{GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH};
    /*主状态机的状态*/
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    /*报文解析结果*/
    enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTIONS};
    /*从状态机的状态*/
    enum LINE_STATUS{LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    http_conn();
    ~http_conn();

public:
    /*初始化套接字地址，函数内部调用私有方法init*/
    void init(int sockfd, const sockaddr_in &addr);
    /*关闭http连接*/
    void close_conn(bool real_close = true);
    void process();
    /*读取浏览器端发来的全部数据*/
    bool read_once();
    /*响应报文写入函数*/
    bool write();
    sockaddr_in *get_address() {
        return *m_address;
    }
    /*同步线程初始化数据库读取表*/
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;

private:
    void init();
    /*从m_read_buf读取，并处理请求报文*/
    HTTP_CODE process_read();
    /*向m_write_buf写入响应报文*/
    bool process_write(HTTP_CODE ret);
    /*主状态机解析报文中的请求行数据*/
    HTTP_CODE parse_request_line(char *text);
    /*主状态机解析请求报文中的请求头数据*/
    HTTP_CODE parse_headers(char *text);
    /*主状态机解析报文中的请求内容*/
    HTTP_CODE parse_content(char *text);
    /*生成响应报文*/
    HTTP_CODE do_request();

    /*m_start_line是已解析的字符*/
    /*get_line用于将指针往后偏移，指向未处理的字符*/
    char *get_line() {return m_read_buf + m_start_line;};
    
    /*从状态机读取一行，分析是请求报文的哪一部分*/
    LINE_STATUS parse_line();

    void unmap();

    /*根据响应报文的格式，生成对应的8个部分，以下函数均由do_request调用*/
    bool add_response(const char* format,...);
    bool add_content(const char* content);
    bool add_status_line(int status,const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_user_count;
};
#endif