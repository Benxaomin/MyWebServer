

Introduction
===============

采用Epoll/IO多路复用，LT + ET 工作模式均实现，对长短连接处理性能友好
采用半同步/半反应堆线程池，使用泛型编程思想，实现了具有高复用性的内部线程池
采用双向链表作为定时器管理结构，使用信号统一事件源机制处理非活动连接
采用同步 + 异步两种方式实现高性能日志库并分级，日志类使用了懒汉单例模式构建，异步使用阻塞队列进行读写
采用单例模式和RAII机制实现了一个内部数据库连接池，实现了服务器的注册与登录功能
采用有限状态机进行HTTP解析，支持GET/POST请求


快速运行
------------
* 服务器测试环境
	* Ubuntu版本20.04
	* MySQL版本8.0
* 浏览器测试环境
	* Windows、Linux均可
	* Chrome
	* Edge

* 测试前确认已安装MySQL数据库

    ```C++
    // 建立yourdb库
    create database yourdb;

    // 创建user表
    USE yourdb;
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;

    // 添加数据
    INSERT INTO user(username, passwd) VALUES('name', 'passwd');
    ```

* 修改main.cpp中的数据库初始化信息

    ```C++
    //数据库登录名,密码,库名
    string user = "root";
    string passwd = "root";
    string databasename = "yourdb";
    ```

* build

    ```C++
    sh ./build.sh
    ```

* 启动server

    ```C++
    ./server
    ```

* 浏览器端

    ```C++
    ip:9006
    ```

个性化运行
------

```C++
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```
* -p，自定义端口号
	* 默认9006
* -l，选择日志写入方式，默认同步写入
	* 0，同步写入
	* 1，异步写入
* -m，listenfd和connfd的模式组合，默认使用LT + LT
	* 0，表示使用LT + LT
	* 1，表示使用LT + ET
    * 2，表示使用ET + LT
    * 3，表示使用ET + ET
* -o，优雅关闭连接，默认不使用
	* 0，不使用
	* 1，使用
* -s，数据库连接数量
	* 默认为8
* -t，线程数量
	* 默认为8
* -c，关闭日志，默认打开
	* 0，打开日志
	* 1，关闭日志
* -a，选择反应堆模型，默认Proactor
	* 0，Proactor模型
	* 1，Reactor模型

测试示例命令与含义

```C++
./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1
```

- [x] 端口9007
- [x] 异步写入日志
- [x] 使用LT + LT组合
- [x] 使用优雅关闭连接
- [x] 数据库连接池内有10条连接
- [x] 线程池内有10条线程
- [x] 关闭日志
- [x] Reactor反应堆模型



全流程记录
------------
https://blog.csdn.net/qq_52313711/article/details/136356042
https://blog.csdn.net/qq_52313711/article/details/136195089
https://blog.csdn.net/qq_52313711/article/details/136203992
https://blog.csdn.net/qq_52313711/article/details/136297193
https://blog.csdn.net/qq_52313711/article/details/136321220
https://blog.csdn.net/qq_52313711/article/details/136332054
https://blog.csdn.net/qq_52313711/article/details/136350362


致谢
------------
《Linux高性能服务器编程》---- 游双
《Linux多线程服务器编程：使用muduo C++网络库》----陈硕