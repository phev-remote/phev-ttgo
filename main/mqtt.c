#include "esp_system.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "msg_core.h"
#include "msg_utils.h"
#include "mqtt.h"
#include "lwip/netif.h"
#include "freertos/event_groups.h"
#include "ota.h"


extern const uint8_t mqtt_phev_remote_com_pem_start[] asm("_binary_phevremote_pem_start");
extern const uint8_t mqtt_phev_remote_com_pem_end[] asm("_binary_phevremote_pem_end");

const static char *APP_TAG = "MQTT";

const static int CONNECTED_BIT = BIT0;

static char * CONTROL_TOPIC;

static char * STATUS_TOPIC;

static char * TOPIC_PREFIX;

static EventGroupHandle_t mqtt_event_group;
    
message_t * msg_mqtt_incomingHandler(messagingClient_t *client)
{
    return NULL;
}

void msg_mqtt_asyncIncomingHandler(messagingClient_t *client, message_t *message)
{
    msg_core_call_subs(client, message);
}
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    mqtt_ctx_t * ctx = (mqtt_ctx_t *) event->user_context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(APP_TAG, "MQTT_EVENT_CONNECTED");
            
            msg_id = esp_mqtt_client_subscribe(client, ctx->incoming_topic, 0);
            ESP_LOGI(APP_TAG, "sent subscribe successful to %s msg_id=%d", ctx->incoming_topic,msg_id);
            ctx->messagingClient->connected = 1;
            xEventGroupSetBits(mqtt_event_group, CONNECTED_BIT);
            msg_id = esp_mqtt_client_subscribe(client, CONTROL_TOPIC, 0);
            ESP_LOGI(APP_TAG, "sent subscribe successful to %s msg_id=%d", CONTROL_TOPIC,msg_id);
            
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(APP_TAG, "MQTT_EVENT_DISCONNECTED");
            //esp_mqtt_client_reconnect(client);
            ctx->messagingClient->connected = 0;
            
            esp_mqtt_client_start(client);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(APP_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(APP_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(APP_TAG, "MQTT_EVENT_DATA");
            if(strncmp(event->topic,CONTROL_TOPIC,strlen(CONTROL_TOPIC)) == 0) 
            {
                
                if(event->data_len == 0)
                {
                    LOG_I(APP_TAG,"Control topic - restarting");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    esp_restart();
                }
                if(strncmp(event->data,OTA_FORCE,strlen(OTA_FORCE)) == 0)
                {
                    LOG_I(APP_TAG,"Forced OTA");
                    ota_do_firmware_upgrade(CONFIG_FIRMWARE_UPGRADE_URL,CONFIG_FIRMWARE_FALLBACK_URL);
                }
                
            } else {
                message_t * msg = msg_utils_createMsgTopic(event->topic,(uint8_t *) event->data,event->data_len);
                msg_mqtt_asyncIncomingHandler(ctx->messagingClient,msg);
            
            }
            
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(APP_TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(APP_TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}
int msg_mqtt_start(messagingClient_t *client)
{
    return 0;
}
int msg_mqtt_stop(messagingClient_t *client)
{
    return 0;
}
int msg_mqtt_connect(messagingClient_t *client)
{
    mqtt_ctx_t * ctx = (mqtt_ctx_t *) client->ctx;

    LOG_I(APP_TAG,"MQTT connecting to %s",ctx->url);
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = ctx->url,
        .event_handle = mqtt_event_handler,
        .user_context = (void *) ctx,
    };

    esp_mqtt_client_handle_t mqtt = esp_mqtt_client_init(&mqtt_cfg);
    ctx->mqtt = mqtt;

    mqtt_event_group = xEventGroupCreate();
    xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
    int ret = esp_mqtt_client_start(mqtt);

    xEventGroupWaitBits(mqtt_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    LOG_I(APP_TAG,"MQTT connected");

    return 0; 
}

void msg_mqtt_outgoingHandler(messagingClient_t *client, message_t *message)
{
    LOG_V(APP_TAG,"START - outgoingHandler");
    mqtt_ctx_t * ctx = (mqtt_ctx_t *) client->ctx;
    
    if(client->connected && message->length > 0 && message->data) 
    {
        if(message->topic == NULL)
        {
            LOG_I(APP_TAG,"mqtt topic %s",ctx->outgoing_topic);
            esp_mqtt_client_publish(ctx->mqtt, ctx->outgoing_topic, (char *) message->data, message->length, 1, 0);
        } else {
            char * topic;
            asprintf(&topic,"%s/%s",TOPIC_PREFIX,message->topic);
            LOG_I(APP_TAG,"mqtt topic %s",topic);
            esp_mqtt_client_publish(ctx->mqtt, topic, (char *) message->data, message->length, 1, 0);
        }
        
    }
    LOG_V(APP_TAG,"END - outgoingHandler");
    //return ret;
}
messagingClient_t * msg_mqtt_createMqttClient(mqttSettings_t settings)
{
    LOG_V(APP_TAG,"START - createMqttClient");
    
    messagingSettings_t clientSettings;
    
    mqtt_ctx_t * ctx = malloc(sizeof(mqtt_ctx_t));
    //msg_mqtt_t * mqtt_ctx = malloc(sizeof(msg_mqtt_t));

    ctx->url = strdup(settings.url);

    asprintf(&ctx->incoming_topic,"%s/%s/%s",settings.topic_prefix,settings.device_id,settings.incoming_topic);
    asprintf(&ctx->outgoing_topic,"%s/%s/%s",settings.topic_prefix,settings.device_id,settings.outgoing_topic);
    asprintf(&CONTROL_TOPIC,"%s/%s/control",settings.topic_prefix,settings.device_id);
    asprintf(&STATUS_TOPIC,"%s/%s/%s",settings.topic_prefix,settings.device_id,settings.status_topic);
    asprintf(&TOPIC_PREFIX,"%s/%s",settings.topic_prefix,settings.device_id);
    
    LOG_I(APP_TAG,"Incoming Topic %s : Outgoing Topic %s : Control Topic %s : Status topic %s",ctx->incoming_topic,ctx->outgoing_topic,CONTROL_TOPIC, STATUS_TOPIC); 

   // ctx->incoming_topic = strdup(settings.incoming_topic);
   // ctx->outgoing_topic = strdup(settings.outgoing_topic);

    LOG_D(APP_TAG,"Addr of incoming handler %p", msg_mqtt_incomingHandler);
    
    clientSettings.incomingHandler = msg_mqtt_incomingHandler;
    clientSettings.outgoingHandler = msg_mqtt_outgoingHandler;
    
    clientSettings.start = NULL;
    clientSettings.stop = NULL;
    clientSettings.connect = msg_mqtt_connect;

    clientSettings.ctx = (void *) ctx;

    messagingClient_t * client = msg_core_createMessagingClient(clientSettings);

    ctx->messagingClient = client;

    LOG_V(APP_TAG,"END - createMqttClient");
    
    return client; 

} 