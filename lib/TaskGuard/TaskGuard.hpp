#include <freertos/task.h>

/**
 * @brief RAII风格的任务句柄管理类
 */
class TaskGuard {
    TaskHandle_t& handle;  // 任务句柄的引用

   public:
    explicit TaskGuard(TaskHandle_t& h) : handle(h) {}

    /**
     * @brief 删除任务句柄，释放资源
     */
    ~TaskGuard() {
        if (handle != nullptr) {
            vTaskDelete(handle);
            handle = nullptr;
        }
    }
};