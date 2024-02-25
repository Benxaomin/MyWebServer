#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool() {
    m_FreeConn = 0;
    m_CurConn = 0;
}
connection_pool::~connection_pool() {
    DestroyPool();
}

void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log) {
    cout<<"  数据库连接池初始化";
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for (int i = 0; i < MaxConn; ++i) {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if (con == nullptr) {
            LOG_ERROR("MySQL Error : mysql_init");
            exit(1);
        }
        
        //cout<<"第"<<i<<"次 :before mysql_real_connect"<<endl;
        /*真正的连接函数*/
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        //cout<<"第"<<i<<"次 :after mysql_real_connect"<<endl;
        if (con == nullptr) {
            LOG_ERROR("MySQL Error : mysql_real_connect");
            exit(1);
        }

        connList.push_back(con);
        m_FreeConn++;
    }

    reserve = sem(m_FreeConn);//信号量记录共享资源总量

    m_MaxConn = m_FreeConn;

    cout<<"  数据库连接池初始化完成";
}

MYSQL *connection_pool::Getconnection() {
    MYSQL * con = NULL;
    if (connList.size() == 0) {
        return NULL;
    }
    reserve.wait();

    m_lock.lock();
    con = connList.front();
    connList.pop_front();

    m_FreeConn--;
    m_CurConn++;
    m_lock.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL *con) {
    if (con == nullptr) {
        return false;
    }
    m_lock.lock();
    connList.push_back(con);
    
    m_FreeConn++;
    m_CurConn--;
    m_lock.unlock();
    reserve.post();
    return true;
}

int connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

void connection_pool::DestroyPool() {
    m_lock.lock();
    if (connList.size() > 0) {
        list<MYSQL*>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_FreeConn = 0;
        m_CurConn = 0;
        connList.clear();
    }
    m_lock.unlock();
}


connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->Getconnection();

    conRAII = *SQL;
    poolRALL = connPool;
}

connectionRAII::~connectionRAII() {
    poolRALL ->ReleaseConnection(conRAII);
}
