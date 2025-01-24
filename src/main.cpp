#include <FastLED.h>
#include <WiFi.h>
// #include <BluetoothController.hpp>
#include <HWCDC.h>
#include <time.h>
#include <ButtonController.hpp>
#include <DNSServer.hpp>
#include <LedPresetManager.hpp>
#include <LittleFSController.hpp>
#include <MdnsController.hpp>
#include <OtaController.hpp>
#include <TimeManager.hpp>
#include <WebServerController.hpp>

void setup() {
    esp_log_level_set("*", ESP_LOG_DEBUG);
    const char* LOG_TAG = "SETUP";

    // 初始化串口
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    // while (!Serial) {
    //     delay(10);
    // }
    // USBSerial.begin(115200);
    // USBSerial.setDebugOutput(true);
    // while (!USBSerial) {
    //     delay(10);
    // }

    // 初始化日志
    ESP_LOGI(LOG_TAG, "Serial initialized");
    ESP_LOGD(LOG_TAG, "Flash size: %d", ESP.getFlashChipSize());
    if (ESP.getPsramSize()) {
        ESP_LOGD(LOG_TAG, "PSRAM size: %d", ESP.getPsramSize());
    }
    ESP_LOGD(LOG_TAG, "Free heap: %d", ESP.getFreeHeap());
    ESP_LOGD(LOG_TAG, "CPU freq: %d MHz", ESP.getCpuFreqMHz());

    // 初始化文件系统
    auto& fs = LittleFSController::getInstance();
    if (!fs.init()) {
        ESP_LOGE("SETUP", "Failed to initialize filesystem");
        return;
    }

    // 初始化OTA
    auto& ota = OtaController::getInstance();
    ota.init();

    // 初始化WiFi
    // auto& wifi = WifiController::getInstance();
    // wifi.init();

    // 初始化蓝牙
    // auto& ble = BluetoothController::getInstance();
    // ble.init();

    // 初始化时间管理器
    auto& time = TimeManager::getInstance();
    time.init();

    // 初始化mDNS
    // auto& mdns = MdnsController::getInstance();
    // if (mdns.init("rain")) {
    //     mdns.addService("http", "tcp", 80);
    //     mdns.start();
    // }

    // 初始化DNS服务器
    // auto& dns = AsyncDNSServer::getInstance();
    // dns.addRecord("*", WiFi.softAPIP());

    // 初始化并启动Web服务器
    // auto& web = WebServerController::getInstance();
    // if (web.init()) {
    //     web.start();
    //     ESP_LOGI("SETUP", "Web server started successfully");
    //     web.addApiHandler(
    //         "/api/test",
    //         WebRequestMethod::HTTP_GET,
    //         [](AsyncWebServerRequest* request) {
    //             ESP_LOGI("API", "Received test request");
    //             request->send(200, "text/plain", "Hello, world!");
    //         }
    //     );

    // } else {
    //     ESP_LOGE("SETUP", "Failed to initialize web server");
    // }

    // 初始化LED
    auto& led = LedPresetManager::getInstance();
    led.applyPreset(LedPreset::SYSTEM_STARTUP);

    // 初始化按键
    auto& button = ButtonController::getInstance(45, ButtonType::ACTIVE_HIGH);
    button.setOnShortPress([]() {
        auto& led = LedPresetManager::getInstance();
        led.applyPreset(LedPreset::WIFI_DISCONNECTED);
    });
    button.setOnLongPress(
        []() {
            auto& led = LedPresetManager::getInstance();
            led.applyPreset(LedPreset::WARNING_SOS);
        },
        2000
    );
}

void loop() {
    // 使用FreeRTOS后，loop函数可以为空
    // vTaskDelete(NULL);  // 删除setup创建的任务

    // 测试输出
    Serial.println("Hello, world! from Serial");
    // USBSerial.println("Hello, world! from USBSerial");
    vTaskDelay(pdMS_TO_TICKS(1000));
}