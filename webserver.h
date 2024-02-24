#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           //最大文件描述符数量
const int MAX_EVENT_NUMBER = 10000; //最大事件数量
const int TIMESLOT = 5;             //最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

public:
    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    void trig_mode();
    void log_write();
    void sql_pool();
    void thread_pool();
    void eventListen();

    void timer(int fd, struct sockaddr_in address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int socfd);

    bool dealclientdata();
    bool dealwithsignal(bool &timerout, bool &stop_server);

    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);
    void eventLoop();
public:
    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;

    int m_epollfd;
    int m_pipefd[2];
    http_conn *users;
    /*数据库*/
    connection_pool *m_connPool;
    string m_user;              //登陆数据库用户名
    string m_passWord;          //登录数据库密码
    string m_databaseName;      //登录数据库名
    int m_sql_num;

    /*线程池*/
    threadpool<http_conn> *m_pool;
    int m_thread_num;


    /*epoll_event*/
    epoll_event events[MAX_EVENT_NUMBER];
    
    int m_TRIGMode;
    int m_OPT_LINGER;
    int m_listenfd;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;
    /*定时器*/
    client_data *users_timer;
    Utils utils;
};

#endif