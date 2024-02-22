#include"lst_timer.h"
#include"../http/http_conn.h"

sort_timer_lst::sort_timer_lst() {
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst() {
    util_timer *tmp = head;
    while (tmp) {
        head = tmp -> next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer) {
    if (!timer) {
        return;
    }
    if (!head) {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
    }
    add_timer(timer, head);
}

void sort_timer_lst::adjust_timer(util_timer *timer) {
    if (!timer) {
        return;
    }
    util_timer * tmp = timer->next;
    /*定时器新的超时值任然小于下一个定时器的超时值时，无需调整*/
    if (!tmp || timer->expire < tmp->expire) {
        return;
    }

    /*将定时器从链表取出，重新插入链表*/
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    } else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, head);
    }
}

/*常规双向节点删除*/
void sort_timer_lst::del_timer(util_timer *timer) {
    if (!timer) {
        return;
    }
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head) {
        head = head->next;
        head ->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
    return;
}


/*按升序插入已lst_head为头节点的lst_timer_lst链表中*/
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) {
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    /*timer需要放到尾节点*/
    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        timer = tail;
    }
}

void sort_timer_lst::tick() {
    if (!head) {
        return;
    }
    /*获取时间*/
    time_t cur = time(NULL);
    util_timer *tmp = head;
    /*遍历定时器链表*/
    while (tmp) {
        /*因定时器链表为升序，则如果当前时间小于定时器超时时间，则后面所有定时器都未到期*/
        if (cur < tmp->expire) {
            break;
        }   
        /*如果当前时间超过定时器时间，调用回调函数*/
        tmp->cb_func(tmp->user_data);
        /*设置新的头节点*/
        head = tmp->next;
        if (head) {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd) {
    /*FILE Control函数用于对文件描述符执行各种操作*/
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option |= O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;//返回的是旧的设置以用于恢复改动状态
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    /*开启边缘触发模式*/
    if (TRIGMode == 1) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    setnonblocking(fd);
}

/*信号处理函数*/
void Utils::sig_handler(int sig) {
    /*异步信号处理环境中，信号处理函数可能发生中断并在其他上下文执行，
    而errno是个全局遍历，若在信号处理函数中发生错误可能会影响其他调用
    信号处理函数的线程，所以调用完后恢复原值可以避免这种错误*/
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

/*设置信号函数*/
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    /*创建sigaction结构体*/
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    /*信号处理函数只发送信号值，不作对应的逻辑处理*/
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    /*将所有信号添加到信号集中*/
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != 0);
}

void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

/*定时器回调函数*/
class Utils;
void cb_func(client_data *user_data) {
    /*删除非活动连接在socket上的注册时间*/
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    /*关闭文件描述符*/
    close(user_data->sockfd);

    http_conn::m_user_count--;
}