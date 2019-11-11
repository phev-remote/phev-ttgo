#include "esp_system.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "msg_core.h"
#include "msg_utils.h"
#include "mqtt.h"


const static char *APP_TAG = "MQTT";
    
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
            ESP_LOGI(APP_TAG, "sent subscribe successful, msg_id=%d", msg_id);

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(APP_TAG, "MQTT_EVENT_DISCONNECTED");
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
            message_t * msg = msg_utils_createMsgTopic(event->topic,(uint8_t *) event->data,event->data_len);
            msg_mqtt_asyncIncomingHandler(ctx->messagingClient,msg);
            
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            
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

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = ctx->url,
        .event_handle = mqtt_event_handler,
        .user_context = (void *) ctx,
    };

    esp_mqtt_client_handle_t mqtt = esp_mqtt_client_init(&mqtt_cfg);
    ctx->mqtt = mqtt;
    
    return esp_mqtt_client_start(mqtt);
}

void msg_mqtt_outgoingHandler(messagingClient_t *client, message_t *message)
{
    LOG_V(APP_TAG,"START - outgoingHandler");
    mqtt_ctx_t * ctx = (mqtt_ctx_t *) client->ctx;
    
    int ret = esp_mqtt_client_publish(client, ctx->outgoing_topic, (char *) message->data, message->length, 1, 0);
    LOG_V(APP_TAG,"END - outgoingHandler");
    //return ret;
}
messagingClient_t * msg_mqtt_createMqttClient(mqttSettings_t settings)
{
    LOG_V(APP_TAG,"START - createGcpClient");
    
    messagingSettings_t clientSettings;
    
    mqtt_ctx_t * ctx = malloc(sizeof(mqtt_ctx_t));
    //msg_mqtt_t * mqtt_ctx = malloc(sizeof(msg_mqtt_t));

    ctx->url = strdup(settings.url);
    ctx->incoming_topic = settings.incoming_topic;
    ctx->outgoing_topic = settings.outgoing_topic;


    clientSettings.incomingHandler = msg_mqtt_incomingHandler;
    clientSettings.outgoingHandler = msg_mqtt_outgoingHandler;
    
    clientSettings.start = NULL;
    clientSettings.stop = NULL;
    clientSettings.connect = msg_mqtt_connect;

    clientSettings.ctx = (void *) ctx;

    messagingClient_t * client = msg_core_createMessagingClient(clientSettings);

    ctx->messagingClient = client;

    LOG_V(APP_TAG,"END - createGcpClient");
    
    return client; 

} 