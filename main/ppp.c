/* PPPoS Client Example with GSM (tested with Telit GL865-DUAL-V3)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/uart.h"

#include "netif/ppp/pppos.h"
#include "netif/ppp/pppapi.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "driver/gpio.h"
#include "logger.h"
#include "ppp.h"



/* The examples use simple GSM configuration that you can set via
   'make menuconfig'.
 */
#define BUF_SIZE (1024)
#define GSM_TIMEOUT (5000)

/* Pins used for serial communication with GSM module */
#define UART1_TX_PIN (GPIO_NUM_27)
#define UART1_RX_PIN (GPIO_NUM_26)
#define UART1_RTS_PIN 0
#define UART1_CTS_PIN 0

#define PPP_APN CONFIG_PPP_APN
#define PPP_USER CONFIG_PPP_USER
#define PPP_PASS CONFIG_PPP_PASS

/* UART */
int uart_num = UART_NUM_1;

const static int CONNECTED_BIT = BIT0;
static EventGroupHandle_t ppp_event_group;

/* The PPP control block */
ppp_pcb *ppp;

/* The PPP IP interface */
struct netif ppp_netif;

static const char *TAG = "PPP";

typedef struct {
    const char *cmd;
    uint16_t cmdSize;
    const char *cmdResponseOnOk;
    uint32_t timeoutMs;
} GSM_Cmd;

#define GSM_OK_Str "OK"

#define CONN_STR "AT+CGDCONT=1,\"IP\",\"" PPP_APN "\"\r"

GSM_Cmd GSM_MGR_InitCmds[] = {
    {
        .cmd = "AT\r",
        .cmdSize = sizeof("AT\r") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = GSM_TIMEOUT,
    },
    {
        .cmd = "ATE0\r",
        .cmdSize = sizeof("ATE0\r") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = GSM_TIMEOUT,
    },
    {
        .cmd = "AT+CPIN?\r",
        .cmdSize = sizeof("AT+CPIN?\r") - 1,
        .cmdResponseOnOk = "CPIN: READY",
        .timeoutMs = GSM_TIMEOUT,
    },
    {
        //AT+CGDCONT=1,"IP","apn"
        .cmd = CONN_STR,
        .cmdSize = sizeof(CONN_STR) - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = GSM_TIMEOUT,
    },
    {
        .cmd = "ATDT*99***1#\r",
        .cmdSize = sizeof("ATDT*99***1#\r") - 1,
        .cmdResponseOnOk = "CONNECT",
        .timeoutMs = GSM_TIMEOUT,
    }
};

#define GSM_MGR_InitCmdsSize  (sizeof(GSM_MGR_InitCmds)/sizeof(GSM_Cmd))

/* PPP status callback example */
static void ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    struct netif *pppif = ppp_netif(pcb);
    LWIP_UNUSED_ARG(ctx);

    switch (err_code) {
    case PPPERR_NONE: {
       
        LOG_D(TAG, "status_cb: Connected\n");
#if PPP_IPV4_SUPPORT
        LOG_D(TAG, "   our_ipaddr  = %s\n", ipaddr_ntoa(&pppif->ip_addr));
        LOG_D(TAG, "   his_ipaddr  = %s\n", ipaddr_ntoa(&pppif->gw));
        LOG_D(TAG, "   netmask     = %s\n", ipaddr_ntoa(&pppif->netmask));
#endif /* PPP_IPV4_SUPPORT */
#if PPP_IPV6_SUPPORT
        LOG_D(TAG, "   our6_ipaddr = %s\n", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
#endif /* PPP_IPV6_SUPPORT */

        ip_addr_t dnsserver;
        IP_ADDR4( &dnsserver,8,8,8,8);
        dns_setserver(0, &dnsserver);
        xEventGroupSetBits(ppp_event_group, CONNECTED_BIT);
        break;
    }
    case PPPERR_PARAM: {
        LOG_E(TAG, "status_cb: Invalid parameter\n");
        break;
    }
    case PPPERR_OPEN: {
        LOG_E(TAG, "status_cb: Unable to open PPP session\n");
        break;
    }
    case PPPERR_DEVICE: {
        LOG_E(TAG, "status_cb: Invalid I/O device for PPP\n");
        break;
    }
    case PPPERR_ALLOC: {
        LOG_E(TAG, "status_cb: Unable to allocate resources\n");
        break;
    }
    case PPPERR_USER: {
        LOG_E(TAG, "status_cb: User interrupt\n");
        break;
    }
    case PPPERR_CONNECT: {
        LOG_E(TAG, "status_cb: Connection lost\n");
        esp_restart();
        break;
    }
    case PPPERR_AUTHFAIL: {
        LOG_E(TAG, "status_cb: Failed authentication challenge\n");
        break;
    }
    case PPPERR_PROTOCOL: {
        LOG_E(TAG, "status_cb: Failed to meet protocol\n");
        break;
    }
    case PPPERR_PEERDEAD: {
        LOG_E(TAG, "status_cb: Connection timeout\n");
        break;
    }
    case PPPERR_IDLETIMEOUT: {
        LOG_E(TAG, "status_cb: Idle Timeout\n");
        break;
    }
    case PPPERR_CONNECTTIME: {
        LOG_E(TAG, "status_cb: Max connect time reached\n");
        break;
    }
    case PPPERR_LOOPBACK: {
        LOG_E(TAG, "status_cb: Loopback detected\n");
        break;
    }
    default: {
        LOG_E(TAG, "status_cb: Unknown error code %d\n", err_code);
        break;
    }
    }

    /*
     * This should be in the switch case, this is put outside of the switch
     * case for example readability.
     */

    if (err_code == PPPERR_NONE) {
        return;
    }

    /* ppp_close() was previously called, don't reconnect */
    if (err_code == PPPERR_USER) {
        /* ppp_free(); -- can be called here */
        return;
    }


    /*
     * Try to reconnect in 30 seconds, if you need a modem chatscript you have
     * to do a much better signaling here ;-)
     */
    //ppp_connect(pcb, 30);
    /* OR ppp_listen(pcb); */
}

static u32_t ppp_output_callback(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    LOG_D(TAG, "PPP tx len %d", len);
    return uart_write_bytes(uart_num, (const char *)data, len);
}

static void pppos_client_task()
{

    //vTaskDelay(10000 / portTICK_PERIOD_MS);
    char *data = (char *) malloc(BUF_SIZE);
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    //Configure UART1 parameters
    uart_param_config(uart_num, &uart_config);

    // Configure UART1 pins (as set in example's menuconfig)
    LOG_D(TAG, "Configuring UART1 GPIOs: TX:%d RX:%d RTS:%d CTS: %d",
             UART1_TX_PIN, UART1_RX_PIN, UART1_RTS_PIN, UART1_CTS_PIN);
    uart_set_pin(uart_num, UART1_TX_PIN, UART1_RX_PIN, UART1_RTS_PIN, UART1_CTS_PIN);
    uart_driver_install(uart_num, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0);

    while (1) {
        //init gsm
        int gsmCmdIter = 0;
        while (1) {
            LOG_D(TAG, "%s", GSM_MGR_InitCmds[gsmCmdIter].cmd);
            uart_write_bytes(uart_num, (const char *)GSM_MGR_InitCmds[gsmCmdIter].cmd,
                             GSM_MGR_InitCmds[gsmCmdIter].cmdSize);

            int timeoutCnt = 0;
            while (1) {
                memset(data, 0, BUF_SIZE);
                int len = uart_read_bytes(uart_num, (uint8_t *)data, BUF_SIZE, 500 / portTICK_RATE_MS);
                if (len > 0) {
                    LOG_D(TAG, "%s", data);
                }

                timeoutCnt += 500;
                if (strstr(data, GSM_MGR_InitCmds[gsmCmdIter].cmdResponseOnOk) != NULL) {
                    break;
                }

                if (timeoutCnt > GSM_MGR_InitCmds[gsmCmdIter].timeoutMs) {
                    LOG_E(TAG, "Gsm Init Error");
                    return;
                }
            }
            gsmCmdIter++;

            if (gsmCmdIter >= GSM_MGR_InitCmdsSize) {
                break;
            }
        }

        LOG_I(TAG, "Gsm init end");

        ppp = pppapi_pppos_create(&ppp_netif,
                                  ppp_output_callback, ppp_status_cb, NULL);

        LOG_I(TAG, "After pppapi_pppos_create");

        if (ppp == NULL) {
            LOG_E(TAG, "Error init pppos");
            return;
        }

        pppapi_set_default(ppp);

        LOG_D(TAG, "After pppapi_set_default");

        pppapi_set_auth(ppp, PPPAUTHTYPE_PAP, PPP_USER, PPP_PASS);

        LOG_D(TAG, "After pppapi_set_auth");

        pppapi_connect(ppp, 0);

        LOG_D(TAG, "After pppapi_connect");

        while (1) {
            memset(data, 0, BUF_SIZE);
            int len = uart_read_bytes(uart_num, (uint8_t *)data, BUF_SIZE, 10 / portTICK_RATE_MS);
            if (len > 0) {
                LOG_D(TAG, "PPP rx len %d", len);
                pppos_input_tcpip(ppp, (u8_t *)data, len);
            }
        }

    }
}

void ppp_start_app(void)
{
    ppp_event_group = xEventGroupCreate();
    xEventGroupClearBits(ppp_event_group, CONNECTED_BIT);
    xTaskCreate(&pppos_client_task, "pppos_client_task", 2048, NULL, 5, NULL);
    xEventGroupWaitBits(ppp_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
}
