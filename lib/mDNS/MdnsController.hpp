/**
 * @file MdnsController.hpp
 * @brief mDNS服务控制器的单例实现
 */
#pragma once

#include <ESPmDNS.h>
#include <esp_log.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string>

/**
 * @brief mDNS服务控制器单例类
 */
class MdnsController {
    static constexpr const char* TAG = "MdnsController";

   public:
    /**
     * @brief 获取实例
     * @return 单例引用
     */
    static MdnsController& getInstance() {
        static MdnsController instance;
        return instance;
    }

    /**
     * @brief 初始化mDNS服务控制器
     * @param hostname 设备主机名
     * @return 初始化是否成功
     */
    bool init(const char* hostname) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (isInitialized) {
                ESP_LOGW(TAG, "MdnsController already initialized");
                xSemaphoreGive(mutex);
                return true;
            }

            this->hostname = hostname;
            isInitialized = true;

            ESP_LOGI(TAG, "MdnsController initialized with hostname: %s", hostname);
            xSemaphoreGive(mutex);
            return true;
        }
        return false;
    }

    /**
     * @brief 启动mDNS服务
     * @return 启动是否成功
     */
    bool start() {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (!isInitialized) {
                ESP_LOGE(TAG, "MdnsController not initialized");
                xSemaphoreGive(mutex);
                return false;
            }

            if (isStarted) {
                ESP_LOGW(TAG, "MdnsController already started");
                xSemaphoreGive(mutex);
                return true;
            }

            // 启动mDNS服务
            if (!MDNS.begin(hostname.c_str())) {
                ESP_LOGE(TAG, "Failed to start mDNS service");
                xSemaphoreGive(mutex);
                return false;
            }

            isStarted = true;
            ESP_LOGI(TAG, "MdnsController started");
            xSemaphoreGive(mutex);
            return true;
        }
        return false;
    }

    /**
     * @brief 添加服务
     * @param service 服务名称（如"http"）
     * @param protocol 协议（如"tcp"）
     * @param port 端口号
     */
    void addService(const char* service, const char* protocol, uint16_t port) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (!isInitialized) {
                ESP_LOGE(TAG, "MdnsController not initialized");
                xSemaphoreGive(mutex);
                return;
            }

            MDNS.addService(service, protocol, port);
            ESP_LOGI(TAG, "Added service: %s.%s on port %d", service, protocol, port);
            xSemaphoreGive(mutex);
        }
    }

    /**
     * @brief 停止mDNS服务
     */
    void stop() {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (!isStarted) {
                ESP_LOGW(TAG, "MdnsController not started");
                xSemaphoreGive(mutex);
                return;
            }

            // 结束mDNS服务
            MDNS.end();
            isStarted = false;
            ESP_LOGI(TAG, "MdnsController stopped");
            xSemaphoreGive(mutex);
        }
    }

    ~MdnsController() {
        if (isStarted) {
            stop();
        }
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
    }

   private:
    MdnsController() {
        ESP_LOGI(TAG, "Creating MdnsController instance");
    }

    MdnsController(const MdnsController&) = delete;             // 禁用拷贝构造
    MdnsController& operator=(const MdnsController&) = delete;  // 禁用赋值操作

    /**
     * @brief mDNS服务是否已初始化
     */
    bool isInitialized = false;

    /**
     * @brief mDNS服务是否已启动
     */
    bool isStarted = false;

    /**
     * @brief 设备主机名
     */
    std::string hostname;
    TaskHandle_t taskHandle = nullptr;
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
};