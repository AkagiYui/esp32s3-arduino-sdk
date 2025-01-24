/**
 * @file LedPresetManager.hpp
 * @brief LED预设管理器的单例实现
 * @details 管理LED的预设配置，支持自定义闪烁序列
 */

#pragma once

#include <map>
#include "LedController.hpp"

/**
 * @brief LED预设类型枚举
 */
enum class LedPreset {
    // 系统状态
    SYSTEM_STARTUP,  // 系统启动中
    SYSTEM_READY,    // 系统就绪
    SYSTEM_ERROR,    // 系统错误
    SYSTEM_UPDATE,   // 系统更新中

    // 网络状态
    WIFI_CONNECTING,    // WiFi连接中
    WIFI_CONNECTED,     // WiFi已连接
    WIFI_DISCONNECTED,  // WiFi断开连接

    // 告警状态
    WARNING_NORMAL,  // 普通告警（慢闪）
    WARNING_URGENT,  // 紧急告警（快闪）
    WARNING_SOS,     // SOS告警（SOS闪烁模式）

    // 业务状态
    WORKING,  // 正在工作
    STANDBY,  // 待机状态

    // 其他
    OFF  // 关闭显示
};

/**
 * @brief LED预设配置结构体
 */
struct LedPresetConfig {
    CRGB color;                   // LED颜色
    uint8_t brightness;           // LED亮度
    LedMode mode;                 // LED显示模式
    BlinkSequence blinkSequence;  // 闪烁序列（仅在mode为BLINK时有效）
};

/**
 * @brief LED预设管理器
 */
class LedPresetManager {
   public:
    static LedPresetManager& getInstance() {
        static LedPresetManager instance;
        return instance;
    }

    /**
     * @brief 应用预设配置
     * @param preset 预设类型
     */
    void applyPreset(LedPreset preset) {
        if (!isInitialized) {
            init();
        }

        const auto& config = getPresetConfig(preset);
        auto& led = LedController::getInstance();

        led.setColor(config.color.r, config.color.g, config.color.b);
        led.setBrightness(config.brightness);

        if (config.mode == LedMode::BLINK) {
            led.setBlinkSequence(config.blinkSequence);
        } else {
            led.setMode(config.mode);
        }
    }

   private:
    LedPresetManager() : isInitialized(false) {}

    // 禁用拷贝构造和赋值
    LedPresetManager(const LedPresetManager&) = delete;
    LedPresetManager& operator=(const LedPresetManager&) = delete;

    /**
     * @brief 初始化预设管理器
     */
    void init() {
        if (isInitialized) return;

        LedController::getInstance().init();
        initPresetConfigs();

        isInitialized = true;
    }

    /**
     * @brief 创建慢闪序列（1秒一次）
     */
    BlinkSequence createSlowBlink() {
        BlinkSequence sequence;
        sequence.steps = {
            {true, 1000, maxBrightness},  // 亮1秒
            {false, 1000, 0}              // 灭1秒
        };
        sequence.repeat = true;
        return sequence;
    }

    /**
     * @brief 创建快闪序列（0.2秒一次）
     */
    BlinkSequence createFastBlink() {
        BlinkSequence sequence;
        sequence.steps = {
            {true, 200, maxBrightness},  // 亮0.2秒
            {false, 200, 0}              // 灭0.2秒
        };
        sequence.repeat = true;
        return sequence;
    }

    /**
     * @brief 创建SOS闪烁序列
     * 摩尔斯电码SOS: ... --- ...
     */
    BlinkSequence createSOSBlink() {
        BlinkSequence sequence;
        // S: 三个短闪
        for (int i = 0; i < 3; i++) {
            sequence.steps.push_back({true, 200, maxBrightness});  // 短亮
            sequence.steps.push_back({false, 200, 0});             // 短灭
        }
        sequence.steps.push_back({false, 400, 0});  // 字母间隔

        // O: 三个长闪
        for (int i = 0; i < 3; i++) {
            sequence.steps.push_back({true, 600, maxBrightness});  // 长亮
            sequence.steps.push_back({false, 200, 0});             // 短灭
        }
        sequence.steps.push_back({false, 400, 0});  // 字母间隔

        // S: 三个短闪
        for (int i = 0; i < 3; i++) {
            sequence.steps.push_back({true, 200, maxBrightness});  // 短亮
            sequence.steps.push_back({false, 200, 0});             // 短灭
        }

        sequence.steps.push_back({false, 1000, 0});  // 序列结尾延迟
        sequence.repeat = true;
        return sequence;
    }

    /**
     * @brief 初始化预设配置映射表
     */
    void initPresetConfigs() {
        const uint8_t maxBrightness = 255;
        const uint8_t midBrightness = 128;
        const uint8_t lowBrightness = 64;

        // 系统状态相关预设
        presetConfigs[LedPreset::SYSTEM_STARTUP] = {
            .color = CRGB::Blue, .brightness = midBrightness, .mode = LedMode::BREATHING
        };

        presetConfigs[LedPreset::SYSTEM_READY] = {
            .color = CRGB::Green, .brightness = maxBrightness, .mode = LedMode::SOLID
        };

        presetConfigs[LedPreset::SYSTEM_ERROR] = {
            .color = CRGB::Red,
            .brightness = maxBrightness,
            .mode = LedMode::BLINK,
            .blinkSequence = createFastBlink()
        };

        presetConfigs[LedPreset::SYSTEM_UPDATE] = {
            .color = CRGB::Blue, .brightness = maxBrightness, .mode = LedMode::BREATHING
        };

        // 网络状态相关预设
        presetConfigs[LedPreset::WIFI_CONNECTING] = {
            .color = CRGB::Blue, .brightness = midBrightness, .mode = LedMode::BREATHING
        };

        presetConfigs[LedPreset::WIFI_CONNECTED] = {
            .color = CRGB::Green, .brightness = lowBrightness, .mode = LedMode::SOLID
        };

        presetConfigs[LedPreset::WIFI_DISCONNECTED] = {
            .color = CRGB(255, 165, 0),  // Orange
            .brightness = midBrightness,
            .mode = LedMode::BLINK,
            .blinkSequence = createSlowBlink()
        };

        // 告警状态相关预设
        presetConfigs[LedPreset::WARNING_NORMAL] = {
            .color = CRGB::Yellow,
            .brightness = maxBrightness,
            .mode = LedMode::BLINK,
            .blinkSequence = createSlowBlink()
        };

        presetConfigs[LedPreset::WARNING_URGENT] = {
            .color = CRGB::Red,
            .brightness = maxBrightness,
            .mode = LedMode::BLINK,
            .blinkSequence = createFastBlink()
        };

        presetConfigs[LedPreset::WARNING_SOS] = {
            .color = CRGB::Red,
            .brightness = maxBrightness,
            .mode = LedMode::BLINK,
            .blinkSequence = createSOSBlink()
        };

        // 业务状态相关预设
        presetConfigs[LedPreset::WORKING] = {
            .color = CRGB::Green, .brightness = maxBrightness, .mode = LedMode::BREATHING
        };

        presetConfigs[LedPreset::STANDBY] = {
            .color = CRGB::Blue, .brightness = lowBrightness, .mode = LedMode::SOLID
        };

        // 关闭LED预设
        presetConfigs[LedPreset::OFF] = {
            .color = CRGB::Black, .brightness = 0, .mode = LedMode::OFF
        };
    }

    /**
     * @brief 获取预设配置
     * @param preset 预设类型
     * @return const LedPresetConfig& 预设配置引用
     */
    const LedPresetConfig& getPresetConfig(LedPreset preset) const {
        auto it = presetConfigs.find(preset);
        if (it != presetConfigs.end()) {
            return it->second;
        }
        return presetConfigs.at(LedPreset::OFF);
    }

    bool isInitialized;                                  // 初始化状态标志
    std::map<LedPreset, LedPresetConfig> presetConfigs;  // 预设配置映射表
    static constexpr uint8_t maxBrightness = 255;        // 最大亮度常量
};