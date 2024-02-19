/*
author:Benxaomin
date:20240219
*/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include<iostream>
#include<stdio.h>
#include<pthread.h>
#include<sys/time.h>
#include"../lock/locker.h"

using namespace std;

template<class T>
class block_queue{
public:

    /*初始化阻塞队列*/
    block_queue(int max_size) {
        if (max_size <= 0) {
            exit(-1);
        }

        m_max_size = max_size;
        T* m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    /*删除new出的T数组*/
    ~block_queue() {
        m_mutex.lock();
        if (m_array != NULL) {
            delete []m_array;
        }
        m_mutex.unlock();
    }

    /*清空队列*/
    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    /*判断队列是否已满*/
    bool full() {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    /*判断队列是否为空*/
    bool empty() {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    /*获得队首元素*/
    bool front(T &value) {
        m_mutex.lock();
        /*注意下面的if判断不能用empty，因为empty函数也有加锁操作，加两次锁会导致死锁*/
        if (size == 0) {
            m_mutex.unlock();
            return false;
        }
        //TODO:个人感觉这行逻辑出错，后面部分是原代码  value = m_array[m_front];
        value = m_array[(m_front + 1) % m_max_size];
        m_mutex.unlock();
        return true;
    }

    /*获得队尾元素*/
    bool back(T& value) {
        m_mutex.lock();
        if (size == 0) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    int max_size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    /*往队列中添加元素前需要先将所有使用队列的线程先唤醒*/
    bool push(T& item) {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    bool pop(T& item) {
        m_mutex.lock();
        while (m_size <= 0) {
            if (!cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    bool pop(T& item,int ms_timeout) {
        struct timespec t = {0,0};//tv_sec ：从1970年1月1日 0点到现在的秒数 tv_nsec:tv_sec后面的纳秒数
        struct timeval now = {0,0};//tv_sec: 从1970年1月1日 0点到现在的秒数 tu_usec:tv_sec后面的微妙数
        gettimeofday(&now,nullptr);
        m_mutex.lock();
        if (m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout/1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }
        //TODO:这一块代码的意义不知道在哪里，留着DEBUG
        if (m_size <= 0) {
            m_mutex.unlock();
            return false;
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;


    T* m_array;
    int m_max_size;
    int m_size;
    int m_front;
    int m_back;
};

#endif