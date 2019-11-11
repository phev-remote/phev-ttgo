/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"
#include "time.h"
#include "lwip/netif.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "mqtt.h"

#include "ttgo.h"
#include "ppp.h"
#include "phev.h"

//#define BROKER_URL "mqtt://mqtt.phevremote.com"
#define BROKER_URL "mqtt://35.235.50.162"
#define MAX_WIFI_CLIENT_SSID_LEN 32
#define MAX_WIFI_CLIENT_PASSWORD_LEN 64
const static int CONNECTED_BIT = BIT0;

#define CMD_TOPIC "command"
#define INIT_TOPIC "init"
#define EVENTS_TOPIC "events"
//#define CONFIG_CAR_WIFI_SSID "BTHub6-P535"
//#define CONFIG_CAR_WIFI_PASSWORD "S1mpsons"

#ifndef CONFIG_CAR_WIFI_SSID
#define CONFIG_CAR_WIFI_SSID "REMOTE45cfsc"
#endif


#ifndef CONFIG_CAR_WIFI_PASSWORD
#define CONFIG_CAR_WIFI_PASSWORD "fhcm852767"
#endif


const static char * TAG = "MAIN";

uint8_t DEFAULT_MAC[] = {0,0,0,0,0,0};

static EventGroupHandle_t wifi_event_group;

enum commands { CMD_UNSET, CMD_STATUS, CMD_REGISTER, CMD_HEADLIGHTS, CMD_BATTERY, CMD_AIRCON, CMD_GET_REG_VAL, CMD_DISPLAY_REG };

static esp_err_t wifi_client_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_AP_STACONNECTED: 
    {
        ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
            MAC2STR(event->event_info.sta_connected.mac),
        event->event_info.sta_connected.aid);
        //xEventGroupSetBits(wifi_event_group, AP_CONNECTED_BIT);
        
        break;
    }
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        //xEventGroupClearBits(wifi_event_group, AP_CONNECTED_BIT);
        esp_wifi_connect();
        
        break;
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "Wifi started");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        //esp_restart();
        break;
    default:
        break;
    }
    //mdns_handle_system_event(ctx, event);
    return ESP_OK;
}

void wifi_client_setup(void)
{
    //uint8_t new_mac[8] = {0x24, 0x0d, 0xc2, 0xc2, 0x91, 0x85};
    //esp_base_mac_addr_set(new_mac);
    //vTaskDelay(100 / portTICK_PERIOD_MS);
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_client_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
}
void wifi_conn_init()
{
    //esp_wifi_stop();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_client_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* wifi_config_t wifi_ap_config =  {
        .ap = {
            .ssid = AP_WIFI_SSID,
            .ssid_len = strlen(AP_WIFI_SSID),
            .password = AP_WIFI_PASS,
            .max_connection = AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },            
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_ap_config));
    
    */
    wifi_config_t wifi_config = {
        .sta.ssid = CONFIG_CAR_WIFI_SSID,
        .sta.password = CONFIG_CAR_WIFI_PASSWORD,
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", wifi_config.sta.ssid, wifi_config.sta.password);
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    
    for (struct netif *pri = netif_list; pri != NULL; pri=pri->next)
    {
        ESP_LOGD(TAG, "Interface priority is %c%c%d (" IPSTR "/" IPSTR " gateway " IPSTR ")",
        pri->name[0], pri->name[1], pri->num,
        IP2STR(&pri->ip_addr.u_addr.ip4), IP2STR(&pri->netmask.u_addr.ip4), IP2STR(&pri->gw.u_addr.ip4));
        if(pri->name[0] == 'p') netif_set_default(pri);
    }
    
}
void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "uk.pool.ntp.org");
    sntp_init();
}

static void obtain_time(void)
{
    initialize_sntp();

     vTaskDelay(2000 / portTICK_PERIOD_MS);
    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    while (timeinfo.tm_year < (2019 - 1900)) {
        ESP_LOGI(TAG, "Waiting for system time to be set...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    printf ( "Current local time and date: %s", asctime(&timeinfo) );
}

static int main_eventHandler(phevEvent_t * event)
{
    
    switch (event->type)
    {
        case PHEV_REGISTER_UPDATE: 
        {
            /*
            switch(command)
            {
                case CMD_BATTERY: 
                {
                    if(event->reg == KO_WF_BATT_LEVEL_INFO_REP_EVR)
                    {
                        int batt = phev_batteryLevel(event->ctx);
                        if(batt < 0)
                        {
                            return 0;
                        }
                        printf("Battery level %d\n",batt);
                        exit(0);
                    }
                    break;
                }
                case CMD_DISPLAY_REG:
                {
                    printf("Register : %d Data :",event->reg);
                    for(int i=0;i<event->length;i++)
                    {
                        printf("%02X ",event->data[i]);
                    }
                    printf("\n");
                    break;
                }
                case CMD_GET_REG_VAL: {
                    phevData_t * reg = phev_getRegister(event->ctx, uint_value);
                    if(reg == NULL)
                    {
                        if(wait_for_regs > WAIT_FOR_REG_MAX)
                        {
                            printf("REGISTER TIMEOUT\n");
                            exit(0);
                        }
                        wait_for_regs ++;
                        return 0;
                    }
                    printf("Get register %d : ",uint_value);
                    for(int i=0;i<reg->length;i++)
                    {
                        printf("%02X ",reg->data[i]);
                    }
                    printf("\n");
                    exit(0);
                    break;
                }
            } */
            return 0;
        }
    
        case PHEV_REGISTRATION_COMPLETE: 
        {
            printf("Registration Complete\n");
            return 0;
        }
        case PHEV_CONNECTED:
        {
            return 0;
        }
        case PHEV_STARTED:
        {
            printf("Started\n");
            return 0;
        }
        case PHEV_VIN:
        {
            printf("VIN number : %s\n",event->data);
            
            return 0;
        }
        case PHEV_ECU_VERSION:
        {
            printf("ECU Version : %s\n",event->data);
            /*
            if(command != CMD_UNSET)
            {
                switch(command)
                {
                    case CMD_HEADLIGHTS: {
                        printf("Turning head lights %s : ",(bool_value?"ON":"OFF"));
                        phev_headLights(event->ctx, bool_value, operationCallback);        
                        break;
                    }
                    case CMD_AIRCON: {
                        printf("Turning air conditioning %s : ",(bool_value?"ON":"OFF"));
                        phev_airCon(event->ctx, bool_value, operationCallback);        
                        break;
                    }
                }
            } */
            return 0;
        }
        default: {
            printf("Unhandled command\n");
            return 0;
        }

    }
    return 0;
}
void main_phev_start()
{
    phevCtx_t * ctx;
    const char * host = "192.168.8.46";
    const unsigned char * mac = DEFAULT_MAC;
    const uint16_t port = 8080;
    bool init = false;
    bool verbose = false;

    mqttSettings_t mqtt_settings = {
        .url = BROKER_URL,
        .incoming_topic = CMD_TOPIC,
        .outgoing_topic = EVENTS_TOPIC,
    };
    messagingClient_t * client =  msg_mqtt_createMqttClient(mqtt_settings);
    phevSettings_t settings = {
        .host = host,
        .mac = mac,
        .port = port,
        .registerDevice = true,
        .handler = main_eventHandler,
        .in = client,
    };

    ctx = phev_registerDevice(settings);

    //ctx = phev_init(settings);

    phev_start((phevCtx_t *) ctx);
}

void mqtt_app_start()
{

}

void app_main()
{
    
    init_nvs();

    initTTGoSIM();

    tcpip_adapter_init();
    ppp_start_app();

    obtain_time();

    wifi_conn_init();

    main_phev_start();
}
