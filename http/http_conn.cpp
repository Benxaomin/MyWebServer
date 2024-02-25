#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>


int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

locker m_lock;
map<string,string> users;

void http_conn::initmysql_result(connection_pool *connPool) {
    cout<<" 数据库具体成员初始化";
    /*从池中取一个连接*/
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    /*在user表中检索username，passwd数据，浏览器端输入*/
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    /*从表中检索完整的结果集*/
    MYSQL_RES *result = mysql_store_result(mysql);
    /*返回结果集中的列数*/
    int num_fields = mysql_num_fields(result);
    /*返回所有字段的数组*/
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    /*从结果集中获取下一行，将对应的用户名和密码存入map中*/
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
    cout<<"  数据库具体成员初始化完成";
}

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

/*从内核事件表删除描述符*/
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/*将事件重置为ONESHOT*/
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1) {
        event.events = ev | EPOLLIN | EPOLLET | EPOLLRDHUP;
    } else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
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

/*从状态机，用于分析出一行的内容
  返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN*/
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
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




/*解析http请求行，获得请求方法，目标url及http版本号*/
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }

    /*将该位置改为0，并将前面的数据取出*/
    *m_url++ = '\0';

    /*取出数据，通过与GET和POST比较，确定请求方式*/
    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else 
        return BAD_REQUEST;
    
    /*m_url此时跳过了第一个空格或\t字符，但不知道后面是否还有
      将m_url向后偏移，通过查找，继续跳过空格和\t，指向请求资源的第一个字符*/
    m_url += strspn(m_url, " \t");
    /*同样的方法判断HTTP版本号*/
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    if (strlen(m_url) == 1) {
        strcat(m_url, "judge.html");
    }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/*解析http请求的一个头部信息*/
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    /*判断是空行还是请求头*/
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    /*解析请求头部连接字段*/
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        /*跳过空格和\t字符*/
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            /*如果是长连接，把m_linger设为true*/
            m_linger = true;
        }
    }
    /*解析请求头部内容长度字段*/
    else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    /*解析请求头部HOST字段*/
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_INFO("oop!unkonw header: %s", text);
    }
    return NO_REQUEST;
}

/*判断http请求是否被完整读入*/
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        /*POST请求中最后为输入的用户名和密码*/
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read() {
    /*初始化从状态机状态，HTTP请求解析结果*/
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    /*parse_line为从状态机的具体实现*/
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();

        /*m_checked_idx是每一个数据行在m_read_buf中的起始位置*/
        /*m_checked_idx表示从状态机在m_read_buf中读取的位置*/
        m_start_line = m_checked_idx;

        LOG_INFO("%s", text);
        /*主状态机三种状态转移逻辑转移*/
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                /*解析请求行*/
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                /*解析请求头*/
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST; 
                }
                /*完整解析GET请求后，跳转到报文响应函数*/
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                /*解析消息体*/
                ret = parse_content(text);

                /*完整解析POST请求后，跳转到报文响应函数*/
                if (ret == GET_REQUEST) {
                    return do_request();
                }

                /*解析完消息体即完成了报文解析，为了避免再次进入循环，更新line_status*/
                line_status = LINE_OPEN;
                break;
            }
        
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    /*找到m_url中/的位置*/
    const char *p = strrchr(m_url, '/');

    /*处理CGI*/
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) =='3')) {
        /*根据标志判断是登录检测还是注册检测*/
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char)* 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        /*提取用户名和密码*/
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3') {
            /*如果是注册，先检测数据库中是非有重名的
              没有重名，增加数据*/
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                /*SQL查询*/
                int res = mysql_query(mysql,sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res) {
                    strcpy(m_url, "/log.html");
                } else {
                    strcpy(m_url, "/registerError.html");
                }
            } else {
                strcpy(m_url, "/registerError.html");
            }
        }
        /*如果是登录，直接判断
        若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0*/
        else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url, "/welcome.html");
            } else {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    /*如果请求资源为/0，表示跳转注册界面*/
    if (*(p + 1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");

        /*将网站目录和/register.html进行拼接，更新到m_real_file中*/
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    /*请求资源为/1，表示跳转登录界面*/
    else if (*(p + 1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");

        strncpy(m_real_file + len, m_url_real, sizeof(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    /*如果文件不可读，返回FORBIDDEN_REQUEST*/
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    /*如果是目录，返回BAD_REQUEST*/
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    /*以只读方式获取文件描述符，通过mmap将该文件描述符映射到内存*/
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    /*请求文件存在，可以访问*/
    return FILE_REQUEST;

}
/*释放m_file_address进行内存映射的内存*/
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write() {
    int temp = 0;
    
    /*响应报文为空*/
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1) {
        /*将响应报文状态行，消息头，空行和响应正文发给浏览器端*/
        temp = writev(m_sockfd, m_iv, m_iv_count);

        /*未发送*/
        if (temp < 0) {
            /*判断缓冲区是否满了*/
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        /*第一个iovec头部信息已发完，发第二个*/
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            /*继续发第一个*/
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        /*数据已全部发完*/
        if (bytes_to_send <= 0) {
            unmap();
            /*epoll上重置EPOLLONESHOT事件*/
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            /*请求为长连接*/
            if (m_linger) {
                /*重新初始化HTTP对象*/
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}



/*-----------------------------------------------process_write()模块-----------------------------------------------*/
//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

bool http_conn::add_response(const char *format, ...) {
    /*若写入内容超出m_write_buf大小则报错*/
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    /*定义可变参数列表*/
    va_list arg_list;
    va_start(arg_list, format);

    /*将数据format从可变参数列表写入缓冲区，返回写入数据长度*/
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    /*若写入的数据长度比缓冲区剩下的空间大，则报错*/
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    /*更新m_write_idx位置*/
    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    return true;
}

/*添加状态行*/
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/*添加消息报头，具体的添加长度文本 连接状态 和空行*/
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n",(m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form)) {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form)) {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        } else {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string)) {
                return false;
            }
        }
    }
    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}



void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
