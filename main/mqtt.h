#ifndef _MQTT_H_
#define _MQTT_H_

#include "mqtt_client.h"
#include "msg_core.h"

typedef struct mqttSettings_t
{
    char * url;
    char * incoming_topic;
    char * outgoing_topic;
    char * status_topic;
    char * topic_prefix;
    char * device_id;
    void * ctx;
} mqttSettings_t;

typedef struct mqtt_ctx_t
{
    char * url;
    char * incoming_topic;
    char * outgoing_topic;
    messagingClient_t * messagingClient;
    esp_mqtt_client_handle_t mqtt;
    void * ctx;
    
    
} mqtt_ctx_t;

messagingClient_t * msg_mqtt_createMqttClient(mqttSettings_t settings);
message_t * msg_mqtt_incomingHandler(messagingClient_t *client);
void msg_mqtt_outgoingHandler(messagingClient_t *client, message_t *message);

#endif