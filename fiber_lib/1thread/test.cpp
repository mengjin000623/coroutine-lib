#include "thread.h"
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>  

using namespace sylar;

// 定义线程执行的任务函数
void func()
{
    // 输出当前线程的ID、名称（通过Thread类的静态方法获取）
    std::cout << "id: " << Thread::GetThreadId() << ", name: " << Thread::GetName();
    // 输出当前Thread实例的ID和名称（通过Thread::GetThis()获取当前线程关联的实例）
    std::cout << ", this id: " << Thread::GetThis()->getId() << ", this name: " << Thread::GetThis()->getName() << std::endl;

    sleep(60);// 线程休眠60秒，模拟长时间任务
}

int main() {
    std::vector<std::shared_ptr<Thread>> thrs;// 创建线程指针的向量容器
    // 循环创建5个线程
    for(int i=0;i<5;i++)
    {
        // 使用std::make_shared创建Thread实例，传入任务函数func和线程名
        std::shared_ptr<Thread> thr = std::make_shared<Thread>(&func, "thread_"+std::to_string(i));
        thrs.push_back(thr);// 将线程指针添加到容器中
    }

    // 循环等待所有线程结束
    for(int i=0;i<5;i++)
    {
        thrs[i]->join();// 调用join方法阻塞主线程，等待子线程执行完毕
    }

    return 0;// 主线程返回，程序结束
}