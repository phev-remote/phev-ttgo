#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"
#include "pthread.h"
#include "esp_pthread.h"
#include "time.h"
#include "lwip/netif.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "mqtt.h"
#include "msg_tcpip.h"
#include "msg_utils.h"
#include "logger.h"

#include "ttgo.h"
#include "ppp.h"
#include "phev.h"
#include "ota.h"

#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/netif.h"

#ifndef CONFIG_MQTT_BROKER_URI 
#define CONFIG_MQTT_BROKER_URI "mqtt://test.mosquitto.org"
#endif
#ifndef CONFIG_MQTT_TOPIC_PREFIX 
#define CONFIG_MQTT_TOPIC_PREFIX ""
#endif
#ifndef CONFIG_MQTT_COMMANDS_TOPIC 
#define CONFIG_MQTT_COMMANDS_TOPIC "commands"
#endif
#ifndef CONFIG_MQTT_EVENTS_TOPIC 
#define CONFIG_MQTT_EVENTS_TOPIC "events"
#endif
#ifndef CONFIG_CAR_WIFI_SSID
#define CONFIG_CAR_WIFI_SSID "SSID"
#endif
#ifndef CONFIG_CAR_WIFI_PASSWORD
#define CONFIG_CAR_WIFI_PASSWORD "PASSWORD"
#endif
#ifndef CONFIG_CAR_HOST_IP
#define CONFIG_CAR_HOST_IP "192.168.8.46"
#endif
#ifndef CONFIG_CAR_HOST_PORT
#define CONFIG_CAR_HOST_PORT 8080
#endif
#ifndef CONFIG_MQTT_STATUS_TOPIC
#define CONFIG_MQTT_STATUS_TOPIC "status"
#endif

#ifdef CONFIG_MY18
#define MY18
#endif
#define MAX_WIFI_CLIENT_SSID_LEN 32
#define MAX_WIFI_CLIENT_PASSWORD_LEN 64


const static char * TAG = "MAIN";
uint8_t DEFAULT_MAC[] = {0,0,0,0,0,0};
const static int CONNECTED_BIT = BIT0;

static EventGroupHandle_t wifi_event_group;

enum commands { CMD_UNSET, CMD_STATUS, CMD_REGISTER, CMD_HEADLIGHTS, CMD_BATTERY, CMD_AIRCON, CMD_GET_REG_VAL, CMD_DISPLAY_REG };

static int global_sock = 0;
static void * nvsHandle = NULL;

static int main_eventHandler(phevEvent_t * event);

static int s_retry_num = 0;

static char * g_version = NULL;

static int64_t lastResponseTime = 0;

static void wifi_client_event_handler(void *ctx, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } 
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

void wifi_client_setup(void)
{
    //uint8_t new_mac[8] = {0x24, 0x0d, 0xc2, 0xc2, 0x91, 0x85};
    //esp_base_mac_addr_set(new_mac);
    //vTaskDelay(100 / portTICK_PERIOD_MS);
    wifi_event_group = xEventGroupCreate();
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_client_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
}
void wifi_conn_init()
{
    //esp_wifi_stop();
    ESP_ERROR_CHECK(esp_netif_init());
    
    wifi_event_group = xEventGroupCreate();
    
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_client_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_client_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    wifi_config_t wifi_config = {
        .sta = { 
            .ssid = CONFIG_CAR_WIFI_SSID,
            .password = CONFIG_CAR_WIFI_PASSWORD,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );
    
    //LOG_I(TAG, "start the WIFI SSID:[%s] password:[%s]", wifi_config.sta.ssid, wifi_config.sta.password);
    LOG_I(TAG,"Waiting for WiFi...");

    
    
    
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);

    for (struct netif *pri = netif_list; pri != NULL; pri=pri->next)
    {
        //LOG_I(TAG, "Interface priority is %c%c%d (" IPSTR "/" IPSTR " gateway " IPSTR ")",
        //pri->name[0], pri->name[1], pri->num,
        //IP2STR(&pri->ip_addr.u_addr.ip4), IP2STR(&pri->netmask.u_addr.ip4), IP2STR(&pri->gw.u_addr.ip4));
        if(pri->name[0] == 'p') 
        {
            LOG_I(TAG,"Set PPP priority interface");
            netif_set_default(pri);
        }
    }

    for (struct netif *pri = netif_list; pri != NULL; pri=pri->next)
    {
        LOG_I(TAG, "Interface priority is %c%c%d (" IPSTR "/" IPSTR " gateway " IPSTR ")",
        pri->name[0], pri->name[1], pri->num,
        IP2STR(&pri->ip_addr.u_addr.ip4), IP2STR(&pri->netmask.u_addr.ip4), IP2STR(&pri->gw.u_addr.ip4));
        
        
    }
    
    LOG_I(TAG,"WiFi Connected...");
        
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
    LOG_I(TAG, "Initializing SNTP");
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
        LOG_I(TAG, "Waiting for system time to be set...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    LOG_I(TAG,"Current local time and date: %s", asctime(&timeinfo) );
}

static int main_eventHandler(phevEvent_t * event)
{
    if(!event) 
    {
        return 0;
    }

    lastResponseTime = esp_timer_get_time();

    switch (event->type)
    {
        case PHEV_REGISTER_UPDATE: 
        {
            LOG_I(TAG,"Register : %d",event->reg);
            
            LOG_BUFFER_HEXDUMP(TAG,event->data,event->length,LOG_INFO);

            return 0;
        }
        case PHEV_REGISTER_UPDATE_ACK:
        {
            LOG_I(TAG,"Register %d acknowledged", event->reg);
#ifdef CONFIG_REBOOT_AFTER_ACK
            vTaskDelay((CONFIG_REBOOT_DELAY * 1000) / portTICK_PERIOD_MS);
            esp_restart();
#endif
            break;
        }
        case PHEV_REGISTRATION_COMPLETE: 
        {
            LOG_I(TAG,"Registration Complete\n");
            nvs_set_u8(nvsHandle,"registered",true);
            LOG_I(TAG,"Rebooting...");
            esp_restart();

            return 0;
        }
        case PHEV_CONNECTED:
        {
            LOG_I(TAG,"Connected\n");

            return 0;
        }
        case PHEV_STARTED:
        {
            LOG_I(TAG,"Started\n");
            return 0;
        }
        case PHEV_VIN:
        {
            LOG_I(TAG,"VIN number : %s\n",event->data);
            message_t * version = msg_utils_createMsgTopic("version",(uint8_t *) g_version,strlen(g_version));
            event->ctx->serviceCtx->pipe->pipe->in->publish(event->ctx->serviceCtx->pipe->pipe->in,version);
            return 0;
        }
        case PHEV_ECU_VERSION:
        {
            LOG_I(TAG,"ECU Version : %s\n",event->data);
            return 0;
        }
        case PHEV_DATE_SYNC:
        {
            LOG_I(TAG,"Date sync : 20%d-%d-%d %d:%d:%d",event->data[0],event->data[1],event->data[2],event->data[3],event->data[4],event->data[5]);
            char * str = phev_statusAsJson(event->ctx);
            message_t * status = msg_utils_createMsgTopic("status",(uint8_t *) str,strlen(str));
            event->ctx->serviceCtx->pipe->pipe->in->publish(event->ctx->serviceCtx->pipe->pipe->in,status);
            free(str);
            return 0;
        }
        case PHEV_PING_RESPONSE:
        {
            LOG_D(TAG,"Ping Event");
            return 0;
        }
        case PHEV_FILTERED_MESSAGE:
        {
            LOG_D(TAG,"Filtered message");
            return 0;
        }
        default: {
            LOG_W(TAG,"Unhandled command %d\n",event->type);
            return 0;
        }

    }
    return 0;
}
void main_thread(void * ctx)
{
    phev_start((phevCtx_t *) ctx);
}

static int times = 0;
static bool waiting = false;

void refreshCallback(phev_pipe_ctx_t *ctx, uint8_t reg, void *customCtx)
{
    waiting = false;    
    
    LOG_I(TAG,"******REFRESH ACK************** REG %02X %d",reg,times++);
}
void main_phev_start(bool init, uint64_t * mac,char * deviceId)
{
    phevCtx_t * ctx;
    const char * host = CONFIG_CAR_HOST_IP;
    const uint16_t port = CONFIG_CAR_HOST_PORT;
    
    mqttSettings_t mqtt_settings = {
        .url = CONFIG_MQTT_BROKER_URI,
        .topic_prefix = CONFIG_MQTT_TOPIC_PREFIX,
        .device_id = deviceId,
        .incoming_topic = CONFIG_MQTT_COMMANDS_TOPIC,
        .outgoing_topic = CONFIG_MQTT_EVENTS_TOPIC,
        .status_topic = CONFIG_MQTT_STATUS_TOPIC,
        .ctx = nvsHandle,
    };
 

    messagingClient_t * in_client =  msg_mqtt_createMqttClient(mqtt_settings);

    phevSettings_t settings = {
        .host = host,
        .mac = mac,
        .port = port,
        .registerDevice = init,
        .handler = main_eventHandler,
        .in = in_client,
        
    #ifdef MY18
        .my18 = true,
    #endif
    };

    if(init)
    {
	LOG_I(TAG,"Registering");
        ctx = phev_registerDevice(settings);
    } else {
	LOG_I(TAG,"Already Registered");
        ctx = phev_init(settings);
    }
    
    LOG_I(TAG,"Starting thread");
    xTaskCreate( main_thread, "Main thread task", 8192 , (void *) ctx, tskIDLE_PRIORITY, NULL );

    uint8_t lastPing = 0;
    uint8_t timeout = 0;
    int last = -1;
    while(true)
    {

#ifdef CONFIG_SHOW_DEBUG_INFO
        LOG_I(TAG,"*********** Free heap %ul",xPortGetFreeHeapSize());
        LOG_I(TAG,">>>>>>>>> Ping %02X", ctx->serviceCtx->pipe->pingResponse);
        LOG_I(TAG,"*********** Last command time %lld - Current time %lld", lastResponseTime,esp_timer_get_time());
#endif
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        
        if(esp_timer_get_time() > (lastResponseTime + 1000000))
        {
            if(timeout == CONFIG_PING_TIMEOUT)
            {
                LOG_E(TAG,"Ping timeout rebooting");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                esp_restart();
            } 
            else 
            {
                timeout ++;
            }
        } 
        else 
        {
            timeout = 0;
        }
    }
    
}


void app_main()
{
    esp_app_desc_t * app = esp_ota_get_app_description();

    g_version = app->version;
    LOG_I(TAG,"PHEV TTGO Version %s",app->version);
    char * deviceId = NULL;

    uint8_t mac[6];

    uint8_t registered = false;

    uint8_t forceota = false;
    
    #ifdef CONFIG_CUSTOM_MAC
    uint8_t custom_mac[6];

    custom_mac[0] = ((uint8_t) (CONFIG_CUSTOM_MAC_ADDR >> 40) & 0xff);
    custom_mac[1] = ((uint8_t) (CONFIG_CUSTOM_MAC_ADDR >> 32) & 0xff);
    custom_mac[2] = ((uint8_t) (CONFIG_CUSTOM_MAC_ADDR >> 24) & 0xff);
    custom_mac[3] = ((uint8_t) (CONFIG_CUSTOM_MAC_ADDR >> 16) & 0xff);
    custom_mac[4] = ((uint8_t) (CONFIG_CUSTOM_MAC_ADDR >> 8) & 0xff);
    custom_mac[5] = (uint8_t) CONFIG_CUSTOM_MAC_ADDR & 0xff;
    LOG_I(TAG,"Custom MAC %02x%02x%02x%02x%02x%02x",
        (uint8_t) custom_mac[0], 
        (uint8_t) custom_mac[1],
        (uint8_t) custom_mac[2], 
        (uint8_t) custom_mac[3], 
        (uint8_t) custom_mac[4], 
        (uint8_t) custom_mac[5]);
    
    esp_base_mac_addr_set(custom_mac);
    #else
    esp_efuse_mac_get_default(&mac);
    #endif
    init_nvs();

    esp_err_t err = nvs_open("phev_store", NVS_READWRITE, &nvsHandle);

    err = nvs_get_u8(nvsHandle,"registered", &registered);

    if(err == ESP_ERR_NVS_NOT_FOUND)
    {
        LOG_I(TAG,"Car not registered with this device");
        err = nvs_set_u8(nvsHandle,"registered",registered); 
    }

    err = nvs_get_u8(nvsHandle,"forceota", &forceota);

    if(err != ESP_ERR_NVS_NOT_FOUND)
    {
        if(forceota)
        {
            LOG_I(TAG,"OTA has been forced");

            nvs_set_u8(nvsHandle,"forceota",false); 
    
            ota_do_firmware_upgrade(CONFIG_FIRMWARE_UPGRADE_URL,CONFIG_FIRMWARE_FALLBACK_URL);
        }
    }

#ifdef CONFIG_CUSTOM_DEVICE_ID
    deviceId = strdup(CONFIG_CUSTOM_DEVICE_ID);
#else
    asprintf(&deviceId, "%02x%02x%02x%02x%02x%02x",(unsigned char) mac[0], (unsigned char) mac[1],(unsigned char) mac[2], (unsigned char) mac[3], (unsigned char) mac[4], (unsigned char) mac[5]);
#endif
    LOG_I(TAG,"Device ID %s",deviceId);
    #ifdef MY18
    LOG_I(TAG,"MY18");
    #endif

    initTTGoSIM();

    //esp_netif_init();

    wifi_conn_init();

    ppp_start_app();

    obtain_time();
    
#ifdef CONFIG_FIRMWARE_OTA
    
    LOG_I(TAG,"OTA Switched on in config, checking for latest version");
    char * versionString = ota_get_latest_version(CONFIG_FIRMWARE_VERSION_URL,CONFIG_FIRMWARE_FALLBACK_VERSION_URL);

    if(versionString == NULL)
    {
        LOG_W(TAG,"Could not get latest version from %s or %s",CONFIG_FIRMWARE_VERSION_URL,CONFIG_FIRMWARE_FALLBACK_VERSION_URL);
    } 
    else 
    {
        if(strncmp(versionString,app->version,strlen(app->version)) != 0)
        {
            LOG_I(TAG,"Found another version of firmware, upgrading from %s to %s",app->version,versionString);
            ota_do_firmware_upgrade(CONFIG_FIRMWARE_UPGRADE_URL,CONFIG_FIRMWARE_FALLBACK_URL);
        } 
        else 
        {
            LOG_I(TAG,"Firmware at latest version");
        }
    }
#else
    LOG_I(TAG,"OTA Switched off in config");
#endif

    main_phev_start(!registered,mac,deviceId);

}
