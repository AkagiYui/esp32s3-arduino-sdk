/**
 * @file WS2812Driver.hpp
 * @brief WS2812 LED驱动库的实现
 * @details 使用ESP32的RMT模块实现WS2812协议
 */

#pragma once

#include <driver/gpio.h>
#include <driver/rmt.h>
#include <algorithm>
#include <memory>

/**
 * @brief RGB颜色通道顺序枚举
 */
enum class ColorOrder { RGB, RBG, GRB, GBR, BRG, BGR };

/**
 * @brief RGB颜色结构体
 */
struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    RGB(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0) : r(red), g(green), b(blue) {}

    // 设置颜色值
    void set(uint8_t red, uint8_t green, uint8_t blue) {
        r = red;
        g = green;
        b = blue;
    }

    // 根据给定的颜色顺序获取通道值
    uint8_t getChannel(size_t index, ColorOrder order) const {
        switch (order) {
            case ColorOrder::RGB:
                return index == 0 ? r : (index == 1 ? g : b);
            case ColorOrder::RBG:
                return index == 0 ? r : (index == 1 ? b : g);
            case ColorOrder::GRB:
                return index == 0 ? g : (index == 1 ? r : b);
            case ColorOrder::GBR:
                return index == 0 ? g : (index == 1 ? b : r);
            case ColorOrder::BRG:
                return index == 0 ? b : (index == 1 ? r : g);
            case ColorOrder::BGR:
                return index == 0 ? b : (index == 1 ? g : r);
            default:
                return 0;
        }
    }
};

/**
 * @brief HSV颜色结构体
 */
struct HSV {
    uint8_t h;  // 色相 (0-255)
    uint8_t s;  // 饱和度 (0-255)
    uint8_t v;  // 明度 (0-255)

    HSV(uint8_t hue = 0, uint8_t saturation = 255, uint8_t value = 255)
        : h(hue), s(saturation), v(value) {}

    // 转换为RGB
    RGB toRGB() const {
        RGB rgb;
        if (s == 0) {
            rgb.set(v, v, v);
            return rgb;
        }

        uint8_t region = h / 43;
        uint8_t remainder = (h - (region * 43)) * 6;

        uint8_t p = (v * (255 - s)) >> 8;
        uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
        uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

        switch (region) {
            case 0:
                rgb.set(v, t, p);
                break;
            case 1:
                rgb.set(q, v, p);
                break;
            case 2:
                rgb.set(p, v, t);
                break;
            case 3:
                rgb.set(p, q, v);
                break;
            case 4:
                rgb.set(t, p, v);
                break;
            default:
                rgb.set(v, p, q);
                break;
        }

        return rgb;
    }
};

/**
 * @brief WS2812驱动类
 */
class WS2812Driver {
   public:
    /**
     * @brief 构造函数
     * @param pin GPIO引脚号
     * @param numLeds LED数量
     * @param order 颜色通道顺序
     * @param channel RMT通道号(0-7)
     */
    WS2812Driver(
        gpio_num_t pin,
        size_t numLeds,
        ColorOrder order = ColorOrder::GRB,
        rmt_channel_t channel = RMT_CHANNEL_0
    )
        : pin_(pin),
          numLeds_(numLeds),
          colorOrder_(order),
          channel_(channel),
          brightness_(255),
          isInitialized_(false) {
        leds_ = std::make_unique<RGB[]>(numLeds);
    }

    /**
     * @brief 初始化驱动
     * @return bool 是否初始化成功
     */
    bool init() {
        if (isInitialized_) return true;

        // 配置RMT
        rmt_config_t config = {
            .rmt_mode = RMT_MODE_TX,
            .channel = channel_,
            .gpio_num = pin_,
            .clk_div = 2,  // 80MHz / 2 = 40MHz (25ns per tick)
            .mem_block_num = 1,
            .flags = 0,
            .tx_config = {
                .idle_level = RMT_IDLE_LEVEL_LOW,
                .carrier_en = false,
                .loop_en = false,
                .idle_output_en = true,
            }
        };

        if (rmt_config(&config) != ESP_OK) return false;
        if (rmt_driver_install(channel_, 0, 0) != ESP_OK) return false;

        // 配置RMT为WS2812时序
        // WS2812要求: T0H=0.4us, T0L=0.85us, T1H=0.8us, T1L=0.45us
        rmt_item32_t timing[2] = {
            // T0: 0.4us高电平 + 0.85us低电平
            {{{16, 1, 34, 0}}},  // 16 ticks = 0.4us, 34 ticks = 0.85us
            // T1: 0.8us高电平 + 0.45us低电平
            {{{32, 1, 18, 0}}}  // 32 ticks = 0.8us, 18 ticks = 0.45us
        };

        rmt_write_items(channel_, timing, 2, true);
        rmt_set_tx_loop_mode(channel_, false);

        isInitialized_ = true;
        return true;
    }

    /**
     * @brief 设置指定LED的颜色
     * @param index LED索引
     * @param color RGB颜色值
     */
    void setPixel(size_t index, const RGB& color) {
        if (index >= numLeds_) return;
        leds_[index] = color;
    }

    /**
     * @brief 设置指定LED的颜色(HSV格式)
     * @param index LED索引
     * @param color HSV颜色值
     */
    void setPixelHSV(size_t index, const HSV& color) {
        setPixel(index, color.toRGB());
    }

    /**
     * @brief 设置全局亮度
     * @param brightness 亮度值(0-255)
     */
    void setBrightness(uint8_t brightness) {
        brightness_ = brightness;
    }

    /**
     * @brief 清除所有LED(设置为黑色)
     */
    void clear() {
        std::fill(leds_.get(), leds_.get() + numLeds_, RGB());
    }

    /**
     * @brief 将颜色数据发送到LED
     */
    void show() {
        if (!isInitialized_) return;

        // 准备RMT发送缓冲区
        // 每个LED需要24位，每位需要一个RMT项，所以每个LED需要24个RMT项
        const size_t itemsPerLed = 24;
        const size_t bufferSize = numLeds_ * itemsPerLed;
        auto items = std::make_unique<rmt_item32_t[]>(bufferSize);

        // 转换颜色数据为RMT项
        for (size_t i = 0; i < numLeds_; i++) {
            RGB color = leds_[i];

            // 应用亮度
            if (brightness_ != 255) {
                color.r = (color.r * brightness_) >> 8;
                color.g = (color.g * brightness_) >> 8;
                color.b = (color.b * brightness_) >> 8;
            }

            // 按颜色顺序转换为RMT项
            for (size_t j = 0; j < 3; j++) {
                uint8_t byte = color.getChannel(j, colorOrder_);
                for (size_t k = 0; k < 8; k++) {
                    size_t idx = (i * itemsPerLed) + (j * 8) + k;
                    bool bit = byte & (1 << (7 - k));

                    if (bit) {
                        // 1 位: 0.8us高 + 0.45us低
                        items[idx].level0 = 1;
                        items[idx].duration0 = 32;  // 32 ticks = 0.8us
                        items[idx].level1 = 0;
                        items[idx].duration1 = 18;  // 18 ticks = 0.45us
                    } else {
                        // 0 位: 0.4us高 + 0.85us低
                        items[idx].level0 = 1;
                        items[idx].duration0 = 16;  // 16 ticks = 0.4us
                        items[idx].level1 = 0;
                        items[idx].duration1 = 34;  // 34 ticks = 0.85us
                    }
                }
            }
        }

        // 发送数据
        rmt_write_items(channel_, items.get(), bufferSize, true);
        rmt_wait_tx_done(channel_, portMAX_DELAY);
    }

    /**
     * @brief 析构函数
     */
    ~WS2812Driver() {
        if (isInitialized_) {
            rmt_driver_uninstall(channel_);
        }
    }

   private:
    gpio_num_t pin_;               // GPIO引脚
    size_t numLeds_;               // LED数量
    ColorOrder colorOrder_;        // 颜色通道顺序
    rmt_channel_t channel_;        // RMT通道
    uint8_t brightness_;           // 全局亮度
    bool isInitialized_;           // 初始化标志
    std::unique_ptr<RGB[]> leds_;  // LED颜色缓冲区
};