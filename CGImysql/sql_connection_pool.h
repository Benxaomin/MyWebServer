#ifndef SQL_CONNECTION_POOL
#define SQL_CONNECTION_POOL

#include<iostream>
#include<mysql/mysql.h>
#include<list>

#include"../log/log.h"
#include"../lock/locker.h"
using namespace std;

class connection_pool{
public:
    static connection_pool* Getinstance() {
        static connection_pool connPool;
        return &connPool;
    }
public:
    MYSQL *Getconnection();                 //获取数据库连接
    bool ReleaseConnection(MYSQL *conn);    //释放数据库连接
    int GetFreeConn();                     //获得空余连接数量
    void DestroyPool();                     //销毁所有连接

    /*初始化数据库连接池*/
    void init(string url, string User, string PassWord, string DBName, int MaxConn, int Port, int close_log);

private:
    connection_pool();
    ~connection_pool();

private:
    int m_MaxConn;//最大连接数
    int m_FreeConn;//可用连接数
    int m_CurConn;//已用连接数
    list<MYSQL *> connList;//连接池

    locker m_lock;
    sem reserve;//信号量记录可用资源

public:
    string m_url;//主机地址
    string m_Port;//数据库端口号
    string m_User;//数据库用户名
    string m_PassWord;//数据库密码
    string m_DatabaseName;//数据库名
    int m_close_log;//是否开启日志
};

/*使用RAII技术来保证connPool单例对象的生命周期符合RAII规则*/
class connectionRAII {
public:
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();

private:
    connection_pool *poolRALL;
    MYSQL *conRAII;
};

#endif