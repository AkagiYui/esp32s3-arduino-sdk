#include "esp_log.h"
#include "esp_ota_ops.h"

class OtaController {
    static constexpr const char* TAG = "OtaController";

   public:
    static OtaController& getInstance() {
        static OtaController instance;
        return instance;
    }

    void init() {
        const esp_partition_t* boot_partition = esp_ota_get_boot_partition();
        const esp_partition_t* running_partition = esp_ota_get_running_partition();
        const esp_partition_t* next_update_partition = esp_ota_get_next_update_partition(NULL);

        ESP_LOGD(
            TAG,
            "Boot partition: %s (subtype %d) at offset 0x%x",
            boot_partition->label,
            boot_partition->subtype,
            boot_partition->address
        );

        ESP_LOGD(
            TAG,
            "Running partition: %s (subtype %d) at offset 0x%x",
            running_partition->label,
            running_partition->subtype,
            running_partition->address
        );

        ESP_LOGD(
            TAG,
            "Next update partition: %s (subtype %d) at offset 0x%x",
            next_update_partition->label,
            next_update_partition->subtype,
            next_update_partition->address
        );

        ESP_LOGD(
            TAG,
            "Partition sizes - Boot: %d bytes, Running: %d bytes, Next: %d bytes",
            boot_partition->size,
            running_partition->size,
            next_update_partition->size
        );

        // 检查启动分区和运行分区是否一致
        if (boot_partition != running_partition) {
            ESP_LOGW(TAG, "Boot partition and running partition are different!");
        }

        // 获取 OTA 应用程序状态
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK) {
            ESP_LOGD(TAG, "Current firmware state: %d", ota_state);
        }
    }

   private:
    OtaController() {}

    OtaController(const OtaController&) = delete;
    OtaController& operator=(const OtaController&) = delete;
};