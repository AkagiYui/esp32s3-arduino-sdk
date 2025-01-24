/**
 * @file ButtonController.hpp
 * @brief 按键控制器的单例实现
 */
#pragma once

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <TaskGuard.hpp>
#include <functional>
#include <map>
#include <memory>

/**
 * @brief 按键类型
 */
enum class ButtonType {
    ACTIVE_LOW,  // 按键为低电平有效
    ACTIVE_HIGH  // 按键为高电平有效
};

/**
 * @brief 按键控制器，用于处理按键的按下和松开事件
 */
class ButtonController {
    static constexpr const char* LOG_TAG = "ButtonController";

   public:
    /**
     * @brief 按键配置
     */
    struct ButtonConfig {
        uint32_t debounceTime = 50;          // 消抖时间(按键按下或松开后的延迟时间, ms)
        uint32_t taskStackSize = 2 * 1024U;  // 任务堆栈大小
        UBaseType_t taskPriority = 1;        // 任务优先级
    };

    /**
     * @brief 长按配置
     */
    struct LongPressConfig {
        std::function<void()> callback;  // 长按回调
        uint32_t duration;               // 长按时间
    };

    /**
     * @brief 获取实例
     * @param pin 按键引脚
     * @param type 按键类型
     * @return ButtonController& 控制器单例引用
     */
    static ButtonController& getInstance(uint8_t pin, ButtonType type = ButtonType::ACTIVE_LOW) {
        // 创建一个静态的实例映射表，用于存储已创建的实例 {引脚号: 实例}
        static std::map<uint8_t, std::unique_ptr<ButtonController>> instances;  // 用于存储实例

        // 如果实例不存在，则创建一个新实例
        auto it = instances.find(pin);
        if (it == instances.end()) {
            auto instance = std::unique_ptr<ButtonController>(new ButtonController());
            if (!instance->init(pin, type)) {
                ESP_LOGE(LOG_TAG, "Failed to initialize button controller");
                // 返回一个空实例或抛出异常
            }
            auto [inserted_it, success] = instances.insert({pin, std::move(instance)});
            return *inserted_it->second;
        }
        return *it->second;
    }

    /**
     * @brief 设置按键按下回调
     * @param callback 按下回调函数
     */
    void setOnPress(std::function<void()>&& callback) {
        onPressCallback = std::move(callback);
    }

    /**
     * @brief 设置按键松开回调
     * @param callback 松开回调函数
     */
    void setOnRelease(std::function<void()>&& callback) {
        onReleaseCallback = std::move(callback);
    }

    /**
     * @brief 设置短按回调
     * @param callback 短按回调函数
     */
    void setOnShortPress(std::function<void()>&& callback) {
        onShortPressCallback = std::move(callback);
    }

    /**
     * @brief 设置长按回调
     * @param callback 长按回调函数
     * @param duration 长按时间
     */
    void setOnLongPress(std::function<void()>&& callback, uint32_t duration = 3000) {
        longPressConfig = {std::move(callback), duration};
    }

    /**
     * @brief 设置配置
     * @param newConfig 新配置
     */
    void setConfig(const ButtonConfig& newConfig) {
        config = newConfig;
    }

    bool isPressed() const {
        return digitalRead(buttonPin) == (buttonType == ButtonType::ACTIVE_LOW ? LOW : HIGH);
    }

   private:
    ButtonController() : isInitialized(false), buttonPin(0), buttonType(ButtonType::ACTIVE_LOW) {}

    ButtonController(const ButtonController&) = delete;
    ButtonController& operator=(const ButtonController&) = delete;

    bool init(uint8_t pin, ButtonType type = ButtonType::ACTIVE_LOW) {
        if (isInitialized) return true;

        buttonPin = pin;
        buttonType = type;
        pinMode(buttonPin, type == ButtonType::ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

        taskGuard = std::make_unique<TaskGuard>(taskHandle);
        BaseType_t xReturned = xTaskCreate(
            [](void* param) {
                auto& controller = *static_cast<ButtonController*>(param);
                controller.buttonTask();
            },
            ("button_monitor_" + std::to_string(pin)).c_str(),
            config.taskStackSize,
            this,
            config.taskPriority,
            &taskHandle
        );

        if (xReturned != pdPASS) {
            ESP_LOGE(LOG_TAG, "Failed to create button task for pin %d", pin);
            return false;
        }

        isInitialized = true;
        ESP_LOGI(LOG_TAG, "Button controller initialized on pin %d", pin);
        return true;
    }

    void buttonTask() {
        uint32_t pressStartTime = 0;
        uint32_t lastDebounceTime = 0;
        bool wasPressed = false;
        bool longPressFired = false;
        bool lastButtonState = false;

        while (true) {
            bool currentlyPressed = isPressed();

            if (currentlyPressed != lastButtonState) {
                lastDebounceTime = millis();
            }

            if ((millis() - lastDebounceTime) >= config.debounceTime) {
                if (currentlyPressed != wasPressed) {
                    if (currentlyPressed) {
                        pressStartTime = millis();
                        wasPressed = true;
                        longPressFired = false;
                        if (onPressCallback) onPressCallback();
                    } else {
                        uint32_t pressDuration = millis() - pressStartTime;
                        wasPressed = false;

                        if (onReleaseCallback) onReleaseCallback();

                        if (!longPressFired && pressDuration < longPressConfig.duration) {
                            if (onShortPressCallback) onShortPressCallback();
                        }
                    }
                } else if (currentlyPressed && wasPressed) {
                    uint32_t pressDuration = millis() - pressStartTime;
                    if (pressDuration >= longPressConfig.duration && !longPressFired) {
                        if (longPressConfig.callback) {
                            longPressConfig.callback();
                        }
                        longPressFired = true;
                    }
                }
            }

            lastButtonState = currentlyPressed;
            vTaskDelay(pdMS_TO_TICKS(10));  // 减小延迟以提高响应性
        }
    }

    bool isInitialized;
    uint8_t buttonPin;
    TaskHandle_t taskHandle = nullptr;
    std::unique_ptr<TaskGuard> taskGuard;
    ButtonConfig config;

    std::function<void()> onPressCallback;
    std::function<void()> onReleaseCallback;
    std::function<void()> onShortPressCallback;
    LongPressConfig longPressConfig;
    ButtonType buttonType;
};