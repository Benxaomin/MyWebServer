#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>


int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/*设置fd非阻塞*/
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option =old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/*将内核事件表注册读事件，ET模式,选择开启EPOLLONESHOT*/
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode ==1) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/*初始化http连接请求连接进来的各项参数值*/
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                    int close_log, string user, string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;

    /*将sockfd加入m_epollfd内核事件描述符中，并设置one_shot*/
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    /*当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空*/
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    /*私有无参初始化*/
    init();
}

/*初始化接收新的连接,check_state默认为分析请求行状态*/
void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
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

    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/*循环读套接字上的数据，直到无数据可读或对方关闭连接*/
/*非阻塞ET工作模式下，需要一次性把数据读完*/
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;

    /*LT模式*/
    if (m_TRIGMode == 0) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0) {
            return false;
        }
        return true;
    } else {
        /*ET模式*/
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                /*当errno == EAGAIN或EWOULDBLOCK时，表示没有数据可读，需要稍后尝试*/
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                return false;
            }
            /*对方关闭连接*/
            else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}