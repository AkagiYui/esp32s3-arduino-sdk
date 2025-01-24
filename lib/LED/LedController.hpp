/**
 * @file LedController.hpp
 * @brief LED控制器的单例实现
 * @details 提供LED的颜色、亮度和显示模式控制，支持自定义闪烁序列
 */

#pragma once

#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>

#define LED_PIN 48
#define LED_COUNT 1

/**
 * @brief LED显示状态枚举
 */
enum class LedMode {
    OFF,        // 关闭
    SOLID,      // 单色常亮
    BLINK,      // 自定义闪烁
    BREATHING,  // 呼吸效果
    RAINBOW     // 彩虹色渐变
};

/**
 * @brief 闪烁序列中的单个步骤
 */
struct BlinkStep {
    bool isOn;           // true表示亮，false表示灭
    uint32_t duration;   // 持续时间(毫秒)
    uint8_t brightness;  // 亮度值(0-255)，仅在isOn为true时有效
};

/**
 * @brief LED闪烁序列配置
 */
struct BlinkSequence {
    std::vector<BlinkStep> steps;  // 闪烁步骤序列
    bool repeat;                   // 是否循环播放
};

/**
 * @brief LED控制命令类型
 */
enum class LedCommandType {
    SET_COLOR,          // 设置颜色
    SET_BRIGHTNESS,     // 设置亮度
    SET_MODE,           // 设置显示模式
    SET_BLINK_SEQUENCE  // 设置闪烁序列
};

/**
 * @brief LED命令数据结构
 */
struct LedCommand {
    LedCommandType type;

    union {
        struct {
            uint8_t r, g, b;
        } color;

        uint8_t brightness;
        LedMode mode;
        BlinkSequence* blinkSequence;  // 使用指针避免union大小问题
    } data;
};

/**
 * @brief LED控制器单例类
 */
class LedController {
   public:
    static LedController& getInstance() {
        static LedController instance;
        return instance;
    }

    void init() {
        if (isInitialized) return;

        leds = new CRGB[LED_COUNT];
        FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, LED_COUNT).setCorrection(TypicalLEDStrip);

        xTaskCreate(
            [](void* param) {
                auto& controller = *static_cast<LedController*>(param);
                controller.controlTask();
            },
            "led_control",
            1024 * 3,
            this,
            1,
            &controlTaskHandle
        );

        isInitialized = true;
    }

    void setColor(uint8_t r, uint8_t g, uint8_t b) {
        if (!isInitialized) return;

        LedCommand cmd;
        cmd.type = LedCommandType::SET_COLOR;
        cmd.data.color = {r, g, b};
        xQueueSend(cmdQueue, &cmd, portMAX_DELAY);
    }

    void setBrightness(uint8_t brightness) {
        if (!isInitialized) return;

        LedCommand cmd;
        cmd.type = LedCommandType::SET_BRIGHTNESS;
        cmd.data.brightness = brightness;
        xQueueSend(cmdQueue, &cmd, portMAX_DELAY);
    }

    void setMode(LedMode mode) {
        if (!isInitialized) return;

        LedCommand cmd;
        cmd.type = LedCommandType::SET_MODE;
        cmd.data.mode = mode;
        xQueueSend(cmdQueue, &cmd, portMAX_DELAY);
    }

    /**
     * @brief 设置闪烁序列
     * @param sequence 闪烁序列配置
     */
    void setBlinkSequence(const BlinkSequence& sequence) {
        if (!isInitialized) return;

        // 创建序列副本（确保在堆上分配内存）
        auto* newSequence = new BlinkSequence(sequence);

        LedCommand cmd;
        cmd.type = LedCommandType::SET_BLINK_SEQUENCE;
        cmd.data.blinkSequence = newSequence;
        xQueueSend(cmdQueue, &cmd, portMAX_DELAY);
    }

   private:
    LedController()
        : isInitialized(false), currentMode(LedMode::OFF), currentStepIndex(0), stepStartTime(0) {
        cmdQueue = xQueueCreate(10, sizeof(LedCommand));
        mutex = xSemaphoreCreateMutex();
    }

    ~LedController() {
        cleanup();
    }

    // 禁用拷贝
    LedController(const LedController&) = delete;
    LedController& operator=(const LedController&) = delete;

    void cleanup() {
        if (controlTaskHandle) {
            vTaskDelete(controlTaskHandle);
            controlTaskHandle = nullptr;
        }
        if (cmdQueue) {
            vQueueDelete(cmdQueue);
            cmdQueue = nullptr;
        }
        if (mutex) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
        delete[] leds;
        clearBlinkSequence();
    }

    void clearBlinkSequence() {
        if (currentBlinkSequence) {
            delete currentBlinkSequence;
            currentBlinkSequence = nullptr;
        }
    }

    void controlTask() {
        TickType_t lastWakeTime = xTaskGetTickCount();
        const TickType_t frequency = pdMS_TO_TICKS(20);

        while (true) {
            LedCommand cmd;
            while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
                processCommand(cmd);
            }

            updateLedEffect();
            vTaskDelayUntil(&lastWakeTime, frequency);
        }
    }

    void processCommand(const LedCommand& cmd) {
        xSemaphoreTake(mutex, portMAX_DELAY);

        switch (cmd.type) {
            case LedCommandType::SET_COLOR:
                currentColor = CRGB(cmd.data.color.r, cmd.data.color.g, cmd.data.color.b);
                break;

            case LedCommandType::SET_BRIGHTNESS:
                maxBrightness = cmd.data.brightness;
                if (currentMode == LedMode::SOLID) {
                    FastLED.setBrightness(maxBrightness);
                }
                break;

            case LedCommandType::SET_MODE:
                currentMode = cmd.data.mode;
                effectStartTime = millis();
                hue = 0;
                if (currentMode != LedMode::BLINK) {
                    clearBlinkSequence();
                }
                break;

            case LedCommandType::SET_BLINK_SEQUENCE:
                clearBlinkSequence();
                currentBlinkSequence = cmd.data.blinkSequence;
                currentStepIndex = 0;
                stepStartTime = millis();
                currentMode = LedMode::BLINK;
                break;
        }

        xSemaphoreGive(mutex);
    }

    void updateLedEffect() {
        xSemaphoreTake(mutex, portMAX_DELAY);

        switch (currentMode) {
            case LedMode::OFF:
                leds[0] = CRGB::Black;
                FastLED.setBrightness(0);
                break;

            case LedMode::SOLID:
                leds[0] = currentColor;
                FastLED.setBrightness(maxBrightness);
                break;

            case LedMode::BLINK:
                updateBlinkSequence();
                break;

            case LedMode::BREATHING:
                updateBreathingEffect();
                break;

            case LedMode::RAINBOW:
                updateRainbowEffect();
                break;
        }

        FastLED.show();
        xSemaphoreGive(mutex);
    }

    /**
     * @brief 更新闪烁序列效果
     */
    void updateBlinkSequence() {
        if (!currentBlinkSequence || currentBlinkSequence->steps.empty()) {
            return;
        }

        uint32_t currentTime = millis();
        const auto& currentStep = currentBlinkSequence->steps[currentStepIndex];

        // 检查是否需要切换到下一个步骤
        if (currentTime - stepStartTime >= currentStep.duration) {
            currentStepIndex++;
            stepStartTime = currentTime;

            // 检查序列是否结束
            if (currentStepIndex >= currentBlinkSequence->steps.size()) {
                if (currentBlinkSequence->repeat) {
                    currentStepIndex = 0;  // 循环播放
                } else {
                    currentMode = LedMode::SOLID;  // 序列结束，切换到常亮模式
                    return;
                }
            }
        }

        // 应用当前步骤的状态
        const auto& step = currentBlinkSequence->steps[currentStepIndex];
        if (step.isOn) {
            leds[0] = currentColor;
            FastLED.setBrightness(step.brightness);
        } else {
            FastLED.setBrightness(0);
        }
    }

    void updateBreathingEffect() {
        const uint32_t breathPeriod = 2000;
        uint32_t elapsed = (millis() - effectStartTime) % breathPeriod;
        float ratio = float(elapsed) / float(breathPeriod);
        float brightness = sin(ratio * 2 * PI) * 0.5 + 0.5;

        leds[0] = currentColor;
        FastLED.setBrightness(uint8_t(brightness * maxBrightness));
    }

    void updateRainbowEffect() {
        hue += 1;
        leds[0] = CHSV(hue, 255, 255);
        FastLED.setBrightness(maxBrightness);
    }

    bool isInitialized;
    CRGB* leds;
    CRGB currentColor;
    uint8_t maxBrightness = 255;
    LedMode currentMode;
    uint32_t effectStartTime;
    uint8_t hue;
    QueueHandle_t cmdQueue;
    SemaphoreHandle_t mutex;
    TaskHandle_t controlTaskHandle;

    // 闪烁序列相关
    BlinkSequence* currentBlinkSequence = nullptr;
    size_t currentStepIndex;
    uint32_t stepStartTime;
};