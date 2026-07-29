#include <thread>
#include <condition_variable>
#include <mutex>
#include <windows.h>
#include <vector>
#include <functional>
#include <memory>
#include <queue>
class ThreadPool
{
public:
    // 先禁用拷贝/移动
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    // 准备使用单例模式创建线程池
    ThreadPool(UINT workNum) : workNum(workNum)
    {
        // static 
        workers.reserve(workNum);
        // emplace_back
        workers.emplace_back([&](){
            while (true)
            {
                // if()
                std::function<void()> callBack;
                {
                    std::unique_lock<std::mutex> lock(this->mutex);
                    if (!taskQueue.empty() && !stop)
                    {
                        callBack = taskQueue.front();
                        taskQueue.pop();
                    }
                }

                if(callBack) callBack();
                // 了解一下cv的生效逻辑,了解一下什么叫虚假唤醒
                std::unique_lock<std::mutex> lock(this->mutex);
                cv.wait(lock,[&](){});
            }
            
        });
    }

    void pushTaskToThreadPool(std::function<void()> task)
    {
        std::unique_lock<std::mutex> lock(this->mutex);
        taskQueue.push(task);
        cv.notify_one();
    }

    void releaseThreadPoolResource()
    {
        {
            std::unique_lock<std::mutex> lock(this->mutex);
            stop = false;
            cv.notify_all();
        }
        for(auto& thread : workers)
        {
            // 考虑一下不要detach有没有什么问题
            if(thread.joinable())
                thread.join();
        }
    }

private:
    UINT workNum = 0;
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> taskQueue;
    bool stop = false;

};