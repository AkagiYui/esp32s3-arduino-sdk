#pragma once

#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "esp_log.h"
#include "esp_sntp.h"

// 同步状态枚举
enum class SyncStatus {
    SYNC_STATUS_RESET,    // 复位状态
    SYNC_STATUS_ONGOING,  // 正在同步
    SYNC_STATUS_SUCCESS,  // 同步成功
    SYNC_STATUS_FAIL      // 同步失败
};

class TimeManager {
    static constexpr const char* TAG = "TimeManager";

   public:
    // 获取单例实例
    static TimeManager& getInstance() {
        static TimeManager instance;
        return instance;
    }

    // 初始化时间管理器
    bool init(const char* preference_namespace = "time_manager") {
        ESP_LOGI(TAG, "Initializing TimeManager");

        // 初始化成员变量
        last_sync_timestamp = 0;
        sync_status = SyncStatus::SYNC_STATUS_RESET;
        sync_retry_count = 0;

        // 初始化配置存储
        preferences.begin(preference_namespace, false);

        // 从存储中读取NTP服务器列表
        String saved_servers = preferences.getString("ntp_servers", "");
        if (!saved_servers.isEmpty()) {
            char* server = strtok((char*)saved_servers.c_str(), ",");
            while (server != nullptr) {
                ntp_servers.push_back(std::string(server));
                server = strtok(nullptr, ",");
            }
        } else {
            // 默认使用主流NTP服务器
            ntp_servers = {"ntp.aliyun.com", "ntp.ntsc.ac.cn", "cn.ntp.org.cn"};
            saveNtpServers();
        }

        // 从存储中读取上次同步的时间戳
        last_sync_timestamp = preferences.getLong64("last_sync", 0);

        // 配置SNTP
        ESP_LOGI(TAG, "Configuring SNTP");
        sntp_setoperatingmode(SNTP_OPMODE_POLL);

        // 设置NTP服务器
        updateNtpServers();

        // 设置同步通知回调
        sntp_set_time_sync_notification_cb(timeSyncCallback);

        // 设置同步间隔（1小时）
        sntp_set_sync_interval(3600000);

        // 启动SNTP服务
        sntp_init();

        ESP_LOGI(TAG, "TimeManager initialized successfully");
        return true;
    }

    // 获取当前Unix时间戳
    time_t getCurrentTimestamp() {
        time_t now;
        time(&now);
        return now;
    }

    // 将时间戳转换为指定时区的字符串
    std::string timestampToString(
        time_t timestamp, int timezone_hour, const char* format = "%Y-%m-%d %H:%M:%S"
    ) {
        struct tm timeinfo;
        timestamp += timezone_hour * 3600;  // 调整时区
        gmtime_r(&timestamp, &timeinfo);

        char buffer[64];
        strftime(buffer, sizeof(buffer), format, &timeinfo);

        return std::string(buffer);
    }

    // 将时间戳转换为指定时区的tm结构体
    struct tm timestampToTm(time_t timestamp, int timezone_hour) {
        struct tm timeinfo;
        timestamp += timezone_hour * 3600;  // 调整时区
        gmtime_r(&timestamp, &timeinfo);
        return timeinfo;
    }

    // 设置NTP服务器列表
    bool setNtpServers(const std::vector<std::string>& servers) {
        if (servers.empty()) {
            ESP_LOGE(TAG, "NTP server list cannot be empty");
            return false;
        }

        ESP_LOGI(TAG, "Setting new NTP servers");
        // 只保留前3个服务器
        // ntp_servers = servers;
        ntp_servers.clear();
        for (size_t i = 0; i < servers.size() && i < 3; ++i) {
            ntp_servers.push_back(servers[i]);
        }

        // 保存到持久化存储
        saveNtpServers();

        // 更新SNTP配置
        updateNtpServers();

        return true;
    }

    // 手动触发时间同步
    bool syncTime() {
        ESP_LOGI(TAG, "Manually triggering time sync");
        sync_status = SyncStatus::SYNC_STATUS_ONGOING;
        sync_retry_count = 0;
        sntp_restart();
        return true;
    }

    // 获取当前NTP服务器列表
    std::vector<std::string> getNtpServers() const {
        return ntp_servers;
    }

    // 获取上次同步状态
    SyncStatus getSyncStatus() const {
        return sync_status;
    }

    // 获取距离上次成功同步的秒数
    time_t getSecondsSinceLastSync() {
        if (last_sync_timestamp == 0) {
            return -1;  // 从未成功同步过
        }
        return getCurrentTimestamp() - last_sync_timestamp;
    }

    // 获取上次成功同步的时间戳
    time_t getLastSyncTimestamp() const {
        return last_sync_timestamp;
    }

    // 判断时间是否可信
    bool isTimeReliable() {
        // 如果从未同步过，时间不可信
        if (last_sync_timestamp == 0) {
            return false;
        }

        // 如果距离上次同步超过24小时，时间不可信
        if (getSecondsSinceLastSync() > 24 * 3600) {
            return false;
        }

        return true;
    }

   private:
    TimeManager() {}  // 私有构造函数

    TimeManager(const TimeManager&) = delete;             // 禁止拷贝
    TimeManager& operator=(const TimeManager&) = delete;  // 禁止赋值

    std::vector<std::string> ntp_servers;
    Preferences preferences;
    volatile SyncStatus sync_status;
    volatile time_t last_sync_timestamp;
    volatile uint8_t sync_retry_count;
    static const uint8_t MAX_SYNC_RETRIES = 3;

    // 保存NTP服务器列表到持久化存储
    void saveNtpServers() {
        std::stringstream ss;
        for (size_t i = 0; i < ntp_servers.size(); ++i) {
            if (i > 0) ss << ",";
            ss << ntp_servers[i];
        }
        preferences.putString("ntp_servers", ss.str().c_str());
        ESP_LOGI(TAG, "Saved NTP servers: %s", ss.str().c_str());
    }

    // 更新SNTP服务器配置
    void updateNtpServers() {
        for (size_t i = 0; i < ntp_servers.size() && i < 3; ++i) {
            ESP_LOGI(TAG, "Setting NTP server %d: %s", i + 1, ntp_servers[i].c_str());
            sntp_setservername(i, ntp_servers[i].c_str());
        }
    }

    // 处理同步失败
    void handleSyncFailure() {
        ESP_LOGW(TAG, "Time sync failed, retry count: %d", sync_retry_count);

        if (sync_retry_count < MAX_SYNC_RETRIES) {
            // 增加重试次数并再次尝试
            sync_retry_count++;
            ESP_LOGI(TAG, "Retrying time sync...");
            sntp_restart();
        } else {
            // 超过最大重试次数
            sync_status = SyncStatus::SYNC_STATUS_FAIL;
            ESP_LOGE(TAG, "Time sync failed after %d retries", MAX_SYNC_RETRIES);
        }
    }

    // 时间同步回调函数
    static void timeSyncCallback(struct timeval* tv) {
        TimeManager& instance = TimeManager::getInstance();

        if (tv == nullptr) {
            // 同步失败
            instance.handleSyncFailure();
            return;
        }

        // 同步成功
        instance.sync_status = SyncStatus::SYNC_STATUS_SUCCESS;
        instance.last_sync_timestamp = instance.getCurrentTimestamp();
        instance.sync_retry_count = 0;

        // 保存最后同步时间到持久化存储
        instance.preferences.putLong64("last_sync", instance.last_sync_timestamp);

        ESP_LOGI(TAG, "Time sync completed successfully");
        ESP_LOGI(TAG, "Current timestamp: %ld", (long)instance.last_sync_timestamp);

        char strftime_buf[64];
        struct tm timeinfo;
        time_t temp_time = instance.last_sync_timestamp;
        gmtime_r(&temp_time, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Synchronized time: %s", strftime_buf);
    }
};