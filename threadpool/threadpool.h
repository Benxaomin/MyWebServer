#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool{
public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);
    ~threadpool();

    bool append(T *request, int state);
    bool append_p(T *request);
private:
    void *worker(void *args);
    void run();
private:
    int m_thread_number;    //线程池中的线程数量
    int m_max_requests;     //请求队列中的最大请求数
    phtread_t *m_threads;   //线程池数组
    connection_pool *m_connPool;
    int m_actor_model;      //行为模式

    std::list<T *> m_workqueue;//工作队列
    locker m_queuelocker;   //同步事件队列的锁
    sem m_queuestat;        //同步事件信号量
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests)
    : actor_model(m_actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_connPool(connPool) {
        if (m_thread_number <= 0 || m_max_requests <= 0) {
            throw std::exception();
        }
        m_threads = new pthread_t[m_thread_number];
        if (!m_threads) {
            throw std::exception();
        }
        for (int i = 0; i < thread_number; ++i) {
            /*创造线程执行worker函数，将类的this指针传给worker函数当作参数*/
            if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
                delete []m_threads;
                throw std::exception();
            }
            if (pthread_detach(m_threads[i])) {
                delete []m_threads;
                throw std::exception();
            }
        } 
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template<typename T>
bool threadpool<T>::append(T *request, int state) {
    m_queuelocker.lock();
    if (m_threads.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    /*在本项目中，request通常是http数据类型，而m_state则表示该request是读事件还是写事件*/
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T *request) {
    m_queuelocker.lock();
    if (m_threads.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void* args) {
    threadpool *pool = (threadpool*)args;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue[front];
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }

        if (m_actor_model == 1) {
            /*代表读事件*/
            if (request->m_state == 0) {
                if (request->read_once()) {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag;
                }
            /*代表写事件*/
            } else {
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif