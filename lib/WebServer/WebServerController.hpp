/**
 * @file WebServerController.hpp
 * @brief Web服务器控制器类
 */

#pragma once

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Web服务器控制器类
 *
 * 这个类提供了一个通用的Web服务器实现，支持：
 * - 静态文件服务
 * - API路由注册
 * - SPA（单页应用）路由
 * - 自定义404处理
 */
class WebServerController {
    static constexpr const char* TAG = "WebServerController";

   public:
    // 定义请求处理器函数类型
    using RequestHandler = std::function<void(AsyncWebServerRequest*)>;

    /**
     * @brief 获取单例实例
     * @return 返回WebServerController的单例引用
     */
    static WebServerController& getInstance() {
        static WebServerController instance;
        return instance;
    }

    /**
     * @brief 初始化Web服务器
     * @param webRoot Web根目录路径
     * @param port 服务器端口号，默认80
     * @return 初始化是否成功
     */
    bool init(const char* webRoot = "/wwwroot/", uint16_t port = 80) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (isInitialized) {
                ESP_LOGW(TAG, "Web server already initialized");
                xSemaphoreGive(mutex);
                return true;
            }

            server = std::make_unique<AsyncWebServer>(port);
            if (!server) {
                ESP_LOGE(TAG, "Could not create web server instance");
                xSemaphoreGive(mutex);
                return false;
            }

            // 使用serveStatic设置静态文件服务
            if (webRoot && strlen(webRoot) > 0) {
                server->serveStatic("/", LittleFS, webRoot)
                    .setDefaultFile("index.html")
                    .setCacheControl("max-age=600");
            }

            // 设置默认404处理
            server->onNotFound([this](AsyncWebServerRequest* request) {
                if (notFoundHandler) {
                    notFoundHandler(request);
                } else {
                    // 默认404处理：尝试发送404.html，如果不存在则发送简单文本
                    if (LittleFS.exists("/404.html")) {
                        request->send(LittleFS, "/404.html", "text/html", false);
                    } else {
                        request->send(404, "text/plain", "404");
                    }
                }
            });
            isInitialized = true;

            ESP_LOGI(TAG, "Web server isInitialized, port: %d, root: %s", port, webRoot);
            xSemaphoreGive(mutex);
            return true;
        }
        return false;
    }

    /**
     * @brief 添加API端点处理器
     * @param path API路径
     * @param method HTTP方法
     * @param handler 请求处理函数
     */
    void addApiHandler(const char* path, WebRequestMethod method, RequestHandler handler) {
        if (!isInitialized || !server) {
            ESP_LOGE(TAG, "Web server not initialized");
            return;
        }

        server->on(path, method, [handler](AsyncWebServerRequest* request) { handler(request); });
    }

    /**
     * @brief 设置自定义404处理器
     * @param handler 404请求处理函数
     */
    void setNotFoundHandler(RequestHandler handler) {
        if (!isInitialized || !server) {
            ESP_LOGE(TAG, "Web server not initialized");
            return;
        }

        notFoundHandler = handler;
    }

    /**
     * @brief 启动服务器
     */
    void start() {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (!isInitialized) {
                ESP_LOGE(TAG, "Web server not initialized");
                xSemaphoreGive(mutex);
                return;
            }
            server->begin();
            ESP_LOGI(TAG, "Web server started");
            xSemaphoreGive(mutex);
        }
    }

    /**
     * @brief 停止服务器
     */
    void stop() {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (!isInitialized) {
                ESP_LOGW(TAG, "Web server not initialized");
                xSemaphoreGive(mutex);
                return;
            }
            server->end();
            ESP_LOGI(TAG, "Web server stopped");
            xSemaphoreGive(mutex);
        }
    }

    /**
     * @brief 析构函数
     */
    ~WebServerController() {
        if (isInitialized) {
            stop();
        }
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
    }

   private:
    /**
     * @brief 构造函数（私有）
     */
    WebServerController() {
        ESP_LOGI(TAG, "Creating WebServerController instance");
    }

    // 禁用拷贝构造和赋值操作
    WebServerController(const WebServerController&) = delete;
    WebServerController& operator=(const WebServerController&) = delete;

    bool isInitialized = false;                         // 服务器是否已初始化
    std::unique_ptr<AsyncWebServer> server;             // Web服务器实例
    RequestHandler notFoundHandler;                     // 404处理器
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();  // 互斥锁
};