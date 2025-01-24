/**
 * @file LittleFSController.hpp
 * @brief LittleFS文件系统控制器的单例实现
 */
#pragma once

#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string>
#include <vector>

/**
 * @brief LittleFS文件系统控制器单例类
 */
class LittleFSController {
    static constexpr const char* TAG = "LittleFSController";

   public:
    /**
     * @brief 获取实例
     * @return 单例引用
     */
    static LittleFSController& getInstance() {
        static LittleFSController instance;
        return instance;
    }

    /**
     * @brief 初始化文件系统
     * @return 初始化是否成功
     */
    bool init() {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            if (isInitialized) {
                ESP_LOGW(TAG, "LittleFSController already initialized");
                xSemaphoreGive(mutex);
                return true;
            }

            if (!LittleFS.begin(true)) {
                ESP_LOGE(TAG, "Failed to mount LittleFS");
                xSemaphoreGive(mutex);
                return false;
            }

            isInitialized = true;
            ESP_LOGI(TAG, "LittleFS mounted successfully");
            xSemaphoreGive(mutex);
            return true;
        }
        return false;
    }

    /**
     * @brief 检查文件是否存在
     * @param path 文件路径
     * @return 文件是否存在
     */
    bool exists(const char* path) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            bool exists = LittleFS.exists(path);
            xSemaphoreGive(mutex);
            return exists;
        }
        return false;
    }

    /**
     * @brief 读取文件内容
     * @param path 文件路径
     * @return 文件内容的字符串
     */
    std::string readFile(const char* path) {
        std::string content;
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            File file = LittleFS.open(path, "r");
            if (!file) {
                ESP_LOGE(TAG, "Failed to open file for reading: %s", path);
                xSemaphoreGive(mutex);
                return content;
            }

            // 分配缓冲区
            const size_t bufSize = 128;
            char buf[bufSize];

            // 批量读取
            while (size_t bytesRead = file.read((uint8_t*)buf, bufSize)) {
                content.append(buf, bytesRead);
            }

            file.close();
            xSemaphoreGive(mutex);
        }
        return content;
    }

    /**
     * @brief 写入文件内容
     * @param path 文件路径
     * @param content 要写入的内容
     * @return 写入是否成功
     */
    bool writeFile(const char* path, const char* content) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            File file = LittleFS.open(path, "w");
            if (!file) {
                ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
                xSemaphoreGive(mutex);
                return false;
            }

            size_t written = file.print(content);
            file.close();

            bool success = written == strlen(content);
            if (!success) {
                ESP_LOGE(TAG, "Failed to write complete file: %s", path);
            }
            xSemaphoreGive(mutex);
            return success;
        }
        return false;
    }

    /**
     * @brief 获取文件大小
     * @param path 文件路径
     * @return 文件大小（字节）
     */
    size_t getFileSize(const char* path) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            File file = LittleFS.open(path, "r");
            if (!file) {
                ESP_LOGE(TAG, "Failed to open file: %s", path);
                xSemaphoreGive(mutex);
                return 0;
            }

            size_t size = file.size();
            file.close();
            xSemaphoreGive(mutex);
            return size;
        }
        return 0;
    }

    /**
     * @brief 获取文件MIME类型
     * @param path 文件路径
     * @return MIME类型字符串
     */
    const char* getMimeType(const char* path) {
        const char* ext = strrchr(path, '.');
        if (!ext) {
            return "application/octet-stream";
        }
        ext = ext + 1;

        if (strcasecmp(ext, "html") == 0) return "text/html";
        if (strcasecmp(ext, "htm") == 0) return "text/html";
        if (strcasecmp(ext, "css") == 0) return "text/css";
        if (strcasecmp(ext, "js") == 0) return "application/javascript";
        if (strcasecmp(ext, "json") == 0) return "application/json";
        if (strcasecmp(ext, "png") == 0) return "image/png";
        if (strcasecmp(ext, "jpg") == 0) return "image/jpeg";
        if (strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
        if (strcasecmp(ext, "ico") == 0) return "image/x-icon";
        if (strcasecmp(ext, "svg") == 0) return "image/svg+xml";
        if (strcasecmp(ext, "txt") == 0) return "text/plain";
        if (strcasecmp(ext, "pdf") == 0) return "application/pdf";
        if (strcasecmp(ext, "zip") == 0) return "application/zip";

        return "application/octet-stream";
    }

    /**
     * @brief 列出目录内容
     * @param path 目录路径
     * @return 目录内文件和子目录的列表
     */
    std::vector<std::string> listDir(const char* path = "/") {
        std::vector<std::string> files;
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            File root = LittleFS.open(path);
            if (!root) {
                ESP_LOGE(TAG, "Failed to open directory: %s", path);
                xSemaphoreGive(mutex);
                return files;
            }

            if (!root.isDirectory()) {
                ESP_LOGE(TAG, "Not a directory: %s", path);
                root.close();
                xSemaphoreGive(mutex);
                return files;
            }

            File file = root.openNextFile();
            while (file) {
                files.push_back(file.name());
                file = root.openNextFile();
            }

            root.close();
            xSemaphoreGive(mutex);
        }
        return files;
    }

    /**
     * @brief 删除文件
     * @param path 文件路径
     * @return 删除是否成功
     */
    bool removeFile(const char* path) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            bool success = LittleFS.remove(path);
            if (!success) {
                ESP_LOGE(TAG, "Failed to remove file: %s", path);
            }
            xSemaphoreGive(mutex);
            return success;
        }
        return false;
    }

    /**
     * @brief 格式化文件系统
     * @return 格式化是否成功
     */
    bool format() {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            bool success = LittleFS.format();
            if (!success) {
                ESP_LOGE(TAG, "Failed to format filesystem");
            } else {
                ESP_LOGI(TAG, "Filesystem formatted successfully");
            }
            xSemaphoreGive(mutex);
            return success;
        }
        return false;
    }

    ~LittleFSController() {
        if (isInitialized) {
            LittleFS.end();
        }
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
    }

   private:
    LittleFSController() {
        ESP_LOGI(TAG, "Creating LittleFSController instance");
    }

    LittleFSController(const LittleFSController&) = delete;
    LittleFSController& operator=(const LittleFSController&) = delete;

    /**
     * @brief 文件系统是否已初始化
     */
    bool isInitialized = false;

    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
};