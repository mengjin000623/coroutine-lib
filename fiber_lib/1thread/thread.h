#ifndef _THREAD_H_
#define _THREAD_H_

#include <mutex>
#include <condition_variable>// C++ 标准库提供的线程同步机制， 详细用法见下边 process 模块
#include <functional>     

namespace sylar//将类封装在sylar命名空间中， 防止命名冲突
{


/*
 * 信号量类（ Semaphore） ：
 * 作用： 控制调度线程在无任务时主动挂起， 避免 CPU 空转； 在有新任务时唤醒， 提升调度效率。
 * 原理： 通过条件变量 + count 实现线程阻塞与唤醒的同步机制。
 * 应用场景： 用于线程间同步、 调度器线程休眠与唤醒。
 *
 * 主要用途：
 * - 在 Thread 类中， 主线程通过 Semaphore 等待子线程完成初始化。 创建线程调用wait等待， 子线程进行run函数后signal释放线程
 * - 可扩展用于 IOManager、 Scheduler 等模块中的线程级控制。
 * 
 * 使用流程：
 * 1. 调度器中有任务：
 * - count > 0， 线程执行 P操作wait()， 消耗一个信号（ count--） ， 继续调度。
 * 2. 无可调度任务：
 * - count == 0， 线程 wait() 时进入条件变量等待状态， 挂起节省 CPU。
 * 3. 有新任务加入：
 * - V操作signal() 被调用， count++， cv 通知一个等待线程唤醒。
 * 4. 被唤醒线程再次 wait()， 若 count > 0， 继续执行。
 *
 *
 * 意义： 在多线程协程调度中， 通过信号量有效避免资源浪费， 保障线程响应性。
 */


// 用于线程方法间的同步
class Semaphore 
{
private:
    std::mutex mtx;                //mtx互斥锁， 保护count变量， 防止并发访问冲突
    std::condition_variable cv;    //条件变量（ condition_variable） ,用于线程阻塞与唤醒
    int count;                     //信号计数器， 表示当前可用资源数

public:
    // 信号量初始化为0
    //*构造函数， 构造信号量， 设置初始资源数量count为0， 表示一开始没有资源可用
    //*explicit关键字是防止构造函数被自动转换调动， 必须手动写成清楚
    explicit Semaphore(int count_ = 0) : count(count_) {}
    
    // *P 操作（ wait） （ 等待信号， 若 count==0 则阻塞当前线程）
    void wait() 
    {
        std::unique_lock<std::mutex> lock(mtx);//上锁， 保证对count的访问安全
        /*
            *使用while检查count：
            *1、 防止虚假唤醒， 当cv.wait()被意外唤醒， 但是count仍然是0， 所以需要再检查一次
            *2、 在v操作的时候， cv.notify_one()操作可能会唤醒多个线程（ 因为操作系统调度不保证只唤醒一个真的能获取到资源的线程）
            *所以需要wait中的while循环再次检查count， 避免多线程争用。
        */
        while (count == 0) { //如果没有可用的资源
            cv.wait(lock); // 释放锁并挂起等待signal唤醒
        }
        count--;//获取到资源后， 消耗一个任务信号
    }

    // V 操作（ signal） （ 释放信号， 唤醒一个等待线程）
    void signal() 
    {
        std::unique_lock<std::mutex> lock(mtx);//上锁， 保证对count的访问安全
        count++;                               // 增加一个任务信号
        cv.notify_one();                       // 唤醒一个等待的线程
    }
};

/*
 * Thread 类：
 * 封装 Linux 原生线程 pthread， 提供线程的创建、 启动、 同步、 管理等功能。
 * 在线程启动过程中通过 Semaphore 实现主线程与子线程的启动同步。
 * 是协程库中的一个基础线程模块
 * 一共两种线程: 1 由系统自动创建的主线程 2 由Thread类创建的线程
 *
 * 功能概述：
 * 1. 创建新线程并执行用户提供的任务（ std::function<void()>） 。
 * 2. 管理线程 ID、 线程名称等上下文信息。
 * 3. 使用 Semaphore 实现构造阶段的主-子线程同步：
 * - 主线程在构造函数中阻塞， 直到子线程设置完成 m_id、 m_name 等信息；
 * - 子线程启动后 signal 主线程继续。
 * 4. 提供线程局部访问接口（ GetThis、 GetName、 SetName） 。
 */ 
class Thread //定义线程类， 用于封装pthread线程的创建、 运行、 管理
{
public:
    /*
     * 构造函数： 创建并启动一个新线程
     * cb： 线程启动后要执行的回调函数， 类型为std::function<void()>， 赋值给m_cb
     * name： 设置线程的名字， 供调试或日志使用， 赋值给m_name
     * 内部通过 pthread_create 创建新线程， 启动后会执行 run()。
     * 主线程会通过 Semaphore 等待子线程完成初始化。
     */
    Thread(std::function<void()> cb, const std::string& name);
    // 析构函数： 确保资源释放， 必要时使用 pthread_detach 避免泄露
    ~Thread();
    // 获取线程系统 ID（ TID）
    pid_t getId() const { return m_id; }
    // 获取线程名称
    const std::string& getName() const { return m_name; }
    // 等待线程结束（ 封装 pthread_join） 用于阻塞当前线程， 直到Thread所表示的线程结束。
    void join();

public:
    /*
     *获取当前线程的系统ID
     * 静态方法， 获取当前线程的系统TID
     * 一般通过syscall(SYS_getid)实现
    */
	static pid_t GetThreadId();
    /*
     * 获取当前线程的Thread对象
     * 返回当前线程绑定的Thread*指针
     * 借助thread_local线程局部变量实现
     * 方便在任何函数中获取当前线程的上下文
    */
    static Thread* GetThis();

    //GetName()， 返回当前线程的名字（ 线程局部变量）
    static const std::string& GetName();
    // 设置当前线程的名字
    static void SetName(const std::string& name);

private:
	//线程入口函数（ pthread_create使用）
    static void* run(void* arg);

private:
    pid_t m_id = -1;//线程的系统 TID， 通过 GetThreadId() 设置， 在 run() 函数中由子线程自行设置

    // m_thread 是 Thread 类的成员变量， 类型为 pthread_t， 表示该对象所创建的系统线程句柄。
    // 它由 pthread_create 调用时由操作系统自动赋值， 用于标识并控制对应的子线程。
    // 通过 m_thread， 可以对线程执行 join（ 等待其结束） 、 detach（ 分离资源） 等系统级操作。
    // 每个线程都有一个唯一的 pthread_t 句柄， 由操作系统分配， 用于区分线程实体。
    // 通常主线程会通过 m_thread 成员记录所创建的子线程， 便于后续管理和回收。
    pthread_t m_thread = 0;

    // 线程需要运行的函数
    std::function<void()> m_cb;//用户传入的回调函数， 线程启动后要执行的具体逻辑
    std::string m_name;//当前线程名， 用于调试/日志追踪等
    
    //主线程在构造函数中调用 m_semaphore.wait() 阻塞
    //子线程在 run() 函数中完成初始化后调用 m_semaphore.signal() 唤醒主线程
    //作用： 保证构造函数返回前， 线程已准备好（ 设置好了 m_id、 m_name、 绑定到 thread_local）
    Semaphore m_semaphore;//用于线程间同步
};

























}



#endif
