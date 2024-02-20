#ifndef LOG_H
#define LOG_H

#include<stdio.h>
#include<pthread.h>
#include<ctime>
#include<cstring>
#include<cstdarg>
#include"block_queue.h"
using namespace std;

class Log{
public:
    /*日志单例模式2：创建一个共有静态方法获得实例，并用指针返回*/
    static Log *get_instance() {
        static Log instance;//C++11以后懒汉模式无需加锁，编译器会保证局部静态变量的线程安全
        return &instance;
    }
    static void *flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
    }

    /*日志文件初始化 参数列表：文件名 关闭日志 日志缓冲区大小 日志最大行数 阻塞队列大小（仅异步）*/
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void) {
        m_mutex.lock();
        fflush(m_fp);
        m_mutex.unlock();
    }
private:
    /*日志单例模式1：私有化构造函数，确保外界无法创建新实例*/
    Log();
    ~Log();
private:
    void* async_write_log() {
        string single_log;
        /*循环从阻塞队列里获取资源*/
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);//将c_str()输出到m_fp指向的文件中
            m_mutex.unlock();
        }
    }

private:
    FILE *m_fp;//打开log的文件指针
    long long m_count = 0;//日志行数记录
    bool m_is_async;//是否是异步

    block_queue<string> *m_log_queue;//阻塞队列
    int m_close_log;//关闭日志
    int m_log_buf_size;//日志缓冲区大小
    char *m_buf;//缓冲区
    int m_split_lines;//日志最大行数
    int m_today;//日志按天分类，记录当前是哪一天

    char log_name[128];//log文件名
    char dir_name[128];//地址名

    locker m_mutex;
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#endif