#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "ota.h"
#include "logger.h"

const static char * APP_TAG = "OTA";

esp_err_t do_firmware_upgrade(const char * url)
{
    LOG_V(APP_TAG,"START - Performing firmware upgrade");
    esp_http_client_config_t config = {
        .url = url,
     //   .cert_pem = (char *)server_cert_pem_start,
    };
    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        LOG_I(APP_TAG,"Firmware upgraded - restarting");
    
        esp_restart();
    } else {
        return ESP_FAIL;
    }
    
    LOG_V(APP_TAG,"END - Performing firmware upgrade");
    
    return ESP_OK;
}