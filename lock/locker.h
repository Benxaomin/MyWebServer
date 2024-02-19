/*author:Benxaomin
 *date:20240219
 * */
#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>

using namespace std;
class sem{
public:
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw exception();
        }
    }
    sem(int val) {
        if (sem_init(&m_sem, 0, val) != 0) {
            throw exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    bool post() {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

class locker{
public:
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    /*获得互斥锁的指针*/
    pthread_mutex_t *get() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

class cond{
public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw exception();
        }
    }
    
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex) {
        return pthread_cond_wait(&m_cond, m_mutex) == 0;
    }

    bool timewait(pthread_mutex_t *m_mutex, struct timespec *m_abstime) {
        return pthread_cond_timedwait(&m_cond, m_mutex, m_abstime) == 0;
    }

    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

#endif
