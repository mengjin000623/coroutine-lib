#include "ioscheduler.h"
#include "hook.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <stack>
#include <cstring>
#include <chrono>
#include <thread>
#include <cmath>

static int sock_listen_fd = -1;

void test_accept();
void error(const char *msg)
{
    perror(msg);
    printf("erreur...\n");
    exit(1);
}

void watch_io_read()
{
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

void test_accept()
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    int fd = accept(sock_listen_fd, (struct sockaddr *)&addr, &len);
    if (fd < 0)
    {
        //std::cout << "accept failed, fd = " << fd << ", errno = " << errno << std::endl;
    }
    else
    {
        std::cout << "accepted connection, fd = " << fd << std::endl;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        sylar::IOManager::GetThis()->addEvent(fd, sylar::IOManager::READ, [fd]()
        {
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            while (true)
            {
                int ret = recv(fd, buffer, sizeof(buffer), 0);
                if (ret > 0)
                {
                    // 打印接收到的数据
                    //std::cout << "received data, fd = " << fd << ", data = " << buffer << std::endl;
                    
                    // 【添加模拟计算开销：这里是合适的位置】
                    // 增加任务耗时，让计算开销远大于调度开销
                    // for (int i = 0; i < 50000; ++i) {
                    //     std::sqrt(i * i); // 无意义计算，仅增加耗时
                    // }

                    // 构建HTTP响应
                    // for(int j=0;j<10;j++){
                        const char *response = "HTTP/1.1 200 OK\r\n"
                                            "Content-Type: text/plain\r\n"
                                            "Content-Length: 13\r\n"
                                            "Connection: keep-alive\r\n"
                                            "\r\n"
                                            "1";
                                            // "Hello, World!";
                        
                        // 发送HTTP响应
                        ret = send(fd, response, strlen(response), 0);
                    // }
                   // std::cout << "sent data, fd = " << fd << ", ret = " << ret << std::endl;

                    // 关闭连接
                     close(fd);
                     break;
                }
                if (ret <= 0)
                {
                    if (ret == 0 || errno != EAGAIN)
                    {
                        //std::cout << "closing connection, fd = " << fd << std::endl;
                        close(fd);
                        break;
                    }
                    else if (errno == EAGAIN)
                    {
                        //std::cout << "recv returned EAGAIN, fd = " << fd << std::endl;
                        //std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 延长睡眠时间，避免繁忙等待
                    }
                }
            }
        });
    }
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

void test_iomanager()
{
    int portno = 8080;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 设置套接字
    sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen_fd < 0)
    {
        error("Error creating socket..\n");
    }

    int yes = 1;
    // 解决 "address already in use" 错误
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定套接字并监听连接
    if (bind(sock_listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        error("Error binding socket..\n");

    if (listen(sock_listen_fd, 1024) < 0)
    {
        error("Error listening..\n");
    }

    printf("epoll echo server listening for connections on port: %d\n", portno);
    fcntl(sock_listen_fd, F_SETFL, O_NONBLOCK);
    // sylar::IOManager iom(9);
    sylar::IOManager iom(1);
    iom.addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

int main(int argc, char *argv[])
{
    test_iomanager();
    return 0;
}



// // *单个线程进行测试，用于纯i/o场景性能测试
// #include "ioscheduler.h"
// #include "hook.h"
// #include <unistd.h>
// #include <sys/types.h>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <fcntl.h>
// #include <iostream>
// #include <cstring>
// #include <chrono>

// static int sock_listen_fd = -1;

// void test_accept();
// void error(const char *msg)
// {
//     perror(msg);
//     printf("erreur...\n");
//     exit(1);
// }

// void test_accept()
// {
//     struct sockaddr_in addr;
//     memset(&addr, 0, sizeof(addr));
//     socklen_t len = sizeof(addr);
//     int fd = accept(sock_listen_fd, (struct sockaddr *)&addr, &len);
//     if (fd < 0)
//     {
//         // 忽略accept错误，避免日志干扰测试
//         return;
//     }
    
//     // 设置为非阻塞模式
//     fcntl(fd, F_SETFL, O_NONBLOCK);
    
//     // 添加读事件回调
//     sylar::IOManager::GetThis()->addEvent(fd, sylar::IOManager::READ, [fd]()
//     {
//         char buffer[1024];
//         memset(buffer, 0, sizeof(buffer));
//         int ret = recv(fd, buffer, sizeof(buffer), 0);
        
//         if (ret > 0)
//         {
//             // 【关键修改1：移除计算密集任务，模拟纯I/O场景】
            
//             // 【关键修改2：合并HTTP响应发送，减少系统调用】
//             const char *response = "HTTP/1.1 200 OK\r\n"
//                                 "Content-Type: text/plain\r\n"
//                                 "Content-Length: 130\r\n"  // 13*10=130
//                                 "Connection: keep-alive\r\n"
//                                 "\r\n"
//                                 "Hello, World!Hello, World!Hello, World!Hello, World!Hello, World!"
//                                 "Hello, World!Hello, World!Hello, World!Hello, World!Hello, World!";
            
//             // 单次发送完成所有响应，减少I/O调度开销
//             send(fd, response, strlen(response), 0);
            
//             // 关闭连接并移除事件监听
//             close(fd);
//             sylar::IOManager::GetThis()->delEvent(fd, sylar::IOManager::READ);
//         }
//         else if (ret <= 0)
//         {
//             // 处理连接关闭或错误
//             close(fd);
//             sylar::IOManager::GetThis()->delEvent(fd, sylar::IOManager::READ);
//         }
//     });
    
//     // 重新注册监听事件（继续接受新连接）
//     sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
// }

// void test_iomanager()
// {
//     int portno = 8080;
//     struct sockaddr_in server_addr;

//     // 创建监听套接字
//     sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock_listen_fd < 0)
//         error("Error creating socket..\n");

//     // 允许端口复用
//     int yes = 1;
//     setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

//     // 绑定地址和端口
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(portno);
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     if (bind(sock_listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
//         error("Error binding socket..\n");

//     // 开始监听
//     if (listen(sock_listen_fd, 1024) < 0)
//         error("Error listening..\n");

//     // 设置为非阻塞模式
//     fcntl(sock_listen_fd, F_SETFL, O_NONBLOCK);
    
//     // 【关键修改3：初始化单线程协程调度器】
//     sylar::IOManager iom(1);  // 参数改为1，表示单线程
//     iom.addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
    
//     printf("Single-thread coroutine server listening on port: %d\n", portno);
// }

// int main(int argc, char *argv[])
// {
//     test_iomanager();
//     return 0;
// }
