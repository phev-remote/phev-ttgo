#include "esp_system.h"
#include "esp_wifi.h"
//#include "esp_event_loop.h"
//#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "ota.h"
#include "logger.h"
#include "string.h"

const static char * APP_TAG = "OTA";

esp_err_t ota_http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOG_D(APP_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            LOG_D(APP_TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            LOG_D(APP_TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            LOG_D(APP_TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            LOG_D(APP_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if(evt->data_len > 31)
                {
                    LOG_E(APP_TAG,"Version data lenth too long");
                }
                else 
                {
                    LOG_D(APP_TAG,"Version %s",(char *) evt->data);
                    strncpy(evt->user_data,(char *) evt->data, evt->data_len);
                    LOG_D(APP_TAG,"Returning version %s",(char *) evt->user_data);
                }
                
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            LOG_I(APP_TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            LOG_I(APP_TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

char * ota_get_latest_version(const char * url)
{
    char version[32];

    memset(version,0,sizeof(version));
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = ota_http_event_handle,
        .user_data = &version,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        LOG_I(APP_TAG, "Status = %d, content_length = %d",
           esp_http_client_get_status_code(client),
           esp_http_client_get_content_length(client));
    }
    esp_http_client_cleanup(client);
    return strdup(version);
}
esp_err_t ota_do_firmware_upgrade(const char * url)
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