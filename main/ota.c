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

extern const uint8_t server_cert_pem_start[] asm("_binary_fullchain_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_fullchain_pem_end");

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


esp_http_client_handle_t ota_get_config(const char * url, char * data)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = ota_http_event_handle,
        .user_data = data,
        .cert_pem = (char *)server_cert_pem_start,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    return client;
    
}
char * ota_get_latest_version(const char * url, const char * fallbackUrl)
{
    char version[32];

    memset(version,0,sizeof(version));
    
    esp_http_client_handle_t client = ota_get_config(url,version);

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        int status = esp_http_client_get_status_code(client);

        LOG_I(APP_TAG, "Status = %d, content_length = %d",
           status,
           esp_http_client_get_content_length(client));
        if(status == 200)
        {
            LOG_I(APP_TAG,"Latest version %s",version);
                        
            esp_http_client_cleanup(client);
            return strdup(version);
        } 
        else
        {
            if(status == 404)
            {
                LOG_W(APP_TAG,"Cannot find specific car version txt : %s",url);
                
                esp_http_client_handle_t client = ota_get_config(fallbackUrl,version);

                esp_err_t err = esp_http_client_perform(client);

                if (err == ESP_OK)
                {
                    status = esp_http_client_get_status_code(client);
                    if(status == 200)
                    {
                        LOG_I(APP_TAG,"Default version %s",version);
                        esp_http_client_cleanup(client);
                        return strdup(version);
                    }
                    LOG_E(APP_TAG,"Cannot get default version");
                }
            }
            else
            {
                LOG_E(APP_TAG,"OTA http failure %d",status);
            }
        }
    }
    return NULL;
}
esp_err_t ota_do_firmware_upgrade(const char * url, const char * fallbackUrl)
{
    LOG_V(APP_TAG,"START - Performing firmware upgrade");
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = (char *)server_cert_pem_start,
    };
    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        LOG_I(APP_TAG,"Firmware upgraded - restarting");
    
        esp_restart();
    } else {
         esp_http_client_config_t config = {
            .url = fallbackUrl,
            .cert_pem = (char *)server_cert_pem_start,
        };
        esp_err_t ret = esp_https_ota(&config);
        if (ret == ESP_OK) {
            LOG_I(APP_TAG,"Firmware upgraded to default firmware- restarting");
    
             esp_restart();
        }
        LOG_E(APP_TAG,"Cannot upgrade firmware");
    
    }
    
    LOG_V(APP_TAG,"END - Performing firmware upgrade");
    
    return ESP_OK;
}