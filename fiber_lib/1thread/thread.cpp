/*
 *在 sylar 协程库中， Thread 类封装了系统级线程创建、 管理与同步机制， 配合 Semaphore 实现线程启动的安全同步。 它的设计目的是支持调度器创建多个可控线程执行任务。

 *线程创建： 构造函数中通过 pthread_create 启动新线程， 传入 this 指针供子线程访问上下文。

 *线程同步： 主线程创建 Thread 对象后立即调用 Semaphore::wait() 阻塞， 直到子线程完成初始化后用signal() 唤醒主线程， 确保 m_id、 m_name 设置完毕。

 *线程运行： 新线程以静态函数 run() 为入口， 内部设置线程局部变量 t_thread 和 t_thread_name， 便于运行时识别当前线程及其上下文。

 *任务执行： 线程的执行逻辑通过用户传入的 std::function<void()> m_cb 实现； 在 run 中通过 swap移交局部变量， 确保任务与资源解耦， 提高效率。

 *资源管理： 析构函数中若线程未 join， 则自动 detach 防止资源泄漏； 调用 join() 可等待线程完成。

 *线程局部访问： 支持 Thread::GetThis() 获取当前线程绑定的 Thread 对象， SetName() 和GetName() 用于访问和设置当前线程名。

 *整体而言， Thread 类通过 thread_local 变量、 信号量机制和 C++ 高层封装， 实现了线程安全、 启动同步、 任务可控、 资源自动管理， 是协程调度系统中多线程支持的基础模块。
 *
 *
 * 整体来说：
 *（1）主线程通过Thread类创建一个实例（t），指定任务函数cb和线程名；
 *（2）在Thread的构造函数中会调用pthread_create创建一个子线程，将实例的this指针作为参数传入；
 *（3）pthread_create创建子线程时会指定子线程的执行函数run（静态函数），子线程启动后会从run函数进入，将this指针指向的Thread实例赋值给线程局部变量thread_local
      *--从而实现线程自己可以访问自己的线程元数据；将name赋值到线程局部变量t_thread_name
      *--（thread_local是C++11引入的存储关键字，不再是静态变量的全局唯一副本，而是线程级别的局部变量，生命周期与线程绑定，实现 “线程内全局可见，线程间完全隔离”），
 *（4）子线程通过run函数（静态函数）创建一个在子线程的栈中创建一个局部std :: function<void>对象cb，通过swap将实例中的m_cb持有的资源（用户传入的函数、lambda表达式等）与局部cb进行底层资源交换；因为function是通过内部只能指针管理任务资源的生命周期，如果通过直接赋值，会增加智能指针的引用计数，从而在实例析构时，需要等待局部cb执行完毕并析构后才能释放资源，swap是直接转移资源所有权，不涉及引用计数变更，Thread析构时无需等待任务执行，避免了实例与任务声明周期绑定
 *（5）主线程通过信号量Semaphore的wait函数等待，子线程在初始化完成后通过signal唤醒等待的主线程
 * 7、 整个过程确保了： 主线程创建线程时同步等待， 子线程可以访问主线程构造好的 Thread 对象， 保证任务、 名字、 ID 等一致。
 */
#include "thread.h"

#include <sys/syscall.h> // 使用 syscall 获取真实线程 ID（ TID）
#include <iostream>
#include <unistd.h>  

namespace sylar {// 命名空间封装， 防止符号冲突

/*
 *thread_local介绍：
    * thread_local 是 C++11 引入的存储类关键字，用于定义线程局部变量。其核心特性如下：
    * 线程私有性：被 thread_local 修饰的变量，每个线程都会拥有该变量的独立副本，线程间的变量互不干扰。
    * 生命周期：变量的生命周期与线程一致 —— 线程创建时初始化，线程结束时销毁。
    * 访问范围：变量仅在当前线程内可见，其他线程无法直接访问。
 * 
 */
// thread_local 线程局部变量： 每个线程独立拥有一份， 用于保存该线程的上下文信息。
// t_thread： 指向当前线程对应的 Thread 对象（ 线程生命周期内有效） 。
// - 在子线程启动时由 Thread::run() 中设置为 this；
// - 便于日志系统、 调度器等获取当前线程的上下文；
// - 是线程私有变量， 其他线程无法访问
static thread_local Thread* t_thread          = nullptr;
// t_thread_name： 当前线程的名称（ 线程局部变量， 线程内有效） 。
// - 初始值为 "UNKNOWN"， 可在 Thread::SetName() 或 run() 中进行更新；
// - 用于日志记录、 调试信息中输出线程名。
static thread_local std::string t_thread_name = "UNKNOWN";
// 获取当前系统线程 ID（ TID）
// - 返回真实线程 ID， 用于日志记录、 调试等（ 不是 pthread_t 句柄） 。
// - 使用 syscall(SYS_gettid) 获取 Linux 下的线程号。
pid_t Thread::GetThreadId()
{
	return syscall(SYS_gettid);
}
// 获取当前线程对应的 Thread 对象指针
// - 实现基于 thread_local 的 t_thread， 表示当前线程的上下文。
// - 若该线程未通过 Thread 类创建， 则返回 nullptr。
Thread* Thread::GetThis()
{
    return t_thread;
}
// 获取当前线程名称（ 线程局部变量）
// 返回当前线程绑定的名称字符串， 仅对调用线程自身有效
const std::string& Thread::GetName() 
{
    return t_thread_name;
}

//设置线程名
//先设置 thread_local 的 t_thread_name；
//如果当前线程是 Thread 类创建的， 还同步更新它的 m_name 成员变量。
void Thread::SetName(const std::string &name) 
{
    if (t_thread) //如果这个线程是通过Thread创建的， 需要在thread结构体中更新它的名字
    {
        t_thread->m_name = name;
    }
    t_thread_name = name;//更新线程局部的名字（ 线程作用域有效）
}
//构造函数 —— 创建线程 + 同步
/*
* 构造函数： 创建并启动一个新线程
* cb： 线程启动后要执行的回调函数， 类型为std::function<void()>， 赋值给m_cb
* name： 设置线程的名字， 供调试或日志使用， 赋值给m_name
* 内部通过 pthread_create 创建新线程， 启动后会执行 run()。
* 主线程会通过 Semaphore 等待子线程完成初始化。
*/
Thread::Thread(std::function<void()> cb, const std::string &name): 
m_cb(cb), m_name(name) 
{
    //调用 pthread_create 创建一个子线程。
    //&m_thread 保存新线程的句柄（ pthread_t） 到类成员变量 m_thread
    //nullptr 不指定线程属性， 使用默认设置
    //&Thread::run 子线程的入口函数是 Thread::run(void*)， 必须是 static
    //this 把当前 Thread 对象的地址传给子线程， 以便它能访问成员变量（ 比如 m_cb, m_name）
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) 
    {
        std::cerr << "pthread_create thread fail, rt=" << rt << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    // 等待线程函数完成初始化
    //阻塞主线程， 等子线程完成初始化（ 设置好 m_id、 t_thread 等）
    m_semaphore.wait();
}
//* 析构函数 —— 回收资源
//* 如果在调用析构函数之前，没有pthread_join-->存在m_thread句柄-->调用pthread_detach
//* pthread_join的作用：阻塞等待线程结束，获取退出状态，回收资源；等待子线程结束后立即回收其资源
//* pthread_detach的作用：将线程标记为自动回收，无需等待；将子线程标记为分离状态，子线程结束后，自动由内核回收资源
Thread::~Thread() 
{
    if (m_thread) //如果m_thread为空（ 线程没有被创建） 不执行任何操作
    {
        //pthread_detach为了确保线程在结束后能正确释放资源不会成为“僵尸线程”
        //用于分离线程（ 子线程要脱离主线程的管理） ， 表示当前线程已经不再需要主线程或其他线程通过 pthread_join() 等待。
        //pthread_detach(m_thread)
        //使用m_thread不使用t_thread,因为t_thread只在子线程中有效， m_thread是子线程的成员变量， 在主线程中构造Thread就被赋值
        pthread_detach(m_thread);
        m_thread = 0;//确保它不再指向任何有效的线程句柄， 防止重复调用 pthread_detach() 或其他线程操作
    }
}
//等待线程结束
//封装pthread_join(),等待线程执行完成
//如果失败， 抛出异常
void Thread::join() 
{
    if (m_thread) 
    {
        int rt = pthread_join(m_thread, nullptr);
        if (rt) 
        {
            std::cerr << "pthread_join failed, rt = " << rt << ", name = " << m_name << std::endl;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}
//线程入口函数（ pthread_create()创建的新线程会从这里执行）
//run函数本身是静态函数，不依赖于任何的实例，本身不隶属于任何 Thread 实例，它的执行仅依赖传入的 arg 参数（Thread 实例指针），但不持有实例的任何状态。
//即使 Thread 实例在 run 执行过程中被析构，run 函数仍能继续运行（只要 arg 指针未被非法访问）；
//run 内部定义的所有变量（如 cb）都存储在子线程的栈上，与 Thread 实例的内存空间完全分离。
//t_thread（线程局部指针）和线程名称（t_thread_name）虽然指向 / 关联 Thread 实例的信息，但：它们是线程私有变量，存储在子线程的局部存储区，而非 Thread 实例的内存中；
//t_thread（线程局部指针）和线程名称（t_thread_name）其生命周期与子线程绑定，即使 Thread 实例被析构，只要子线程还在运行，这些变量就有效（但可能成为野指针，需避免访问）；本质上是 “子线程记录自身信息的工具”，而非实例的一部分。
//通过在静态函数中创建一个cb函数对象，，存储在子线程的栈上（现在是子线程在运行run函数），与Thread实例的内容空间完全分离，通过swap可以实现任务与实例的解耦
void* Thread::run(void* arg) 
{
    Thread* thread = (Thread*)arg;//将在构造函数中传入的void*指针强制转换成Thread*类型

    t_thread       = thread;//设置线程局部存储变量， 记录当前线程的Thread对象指针
    t_thread_name  = thread->m_name;//记录当前线程的名字（ 线程局部存储变量， 只在子线程中有效）
    thread->m_id   = GetThreadId();//获取线程的TID
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());//给当前线程设置一个操作系统可见的线程名，
    //创建一个空的函数对象cb， 是一个局部变量，//使得这个函数执行完全独立于主线程thread类的成员。 保证thread类可以释放、 销毁不影响当前子线程的继续执行任务
    std::function<void()> cb;
    //之所以使用swap：
    //std::function 可以存储任意可调用对象（函数指针、lambda、函数对象等），这些对象可能包含堆内存（如捕获了大对象的 lambda）。
    //因为 std::function 内部用了智能指针， 比如 shared_ptr） ， 等执行完 cb() 后还需要销毁一次， 多一次引用计数操作。
    //使用swap， 不会发生引用计数的变化， 而是直接把底层的资源“交换”过来， 效率高， 资源回收快。
    // 将Thread对象中的m_cb(用户传进来的线程任务)和局部cb位置交换
    cb.swap(thread->m_cb); // swap -> 可以减少m_cb中只能指针的引用计数
    
    // 初始化完成
    thread->m_semaphore.signal();

    cb();
    return 0;
}

} 

