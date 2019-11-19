#include "ttgo.h"

void initTTGoSIM(void)
{
    gpio_config_t io_conf;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL<< SIMCARD_RST) | (1ULL<< SIMCARD_PWKEY) | (1ULL << SIM800_POWER_ON);
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;

    gpio_config(&io_conf);
    
    gpio_set_level(SIMCARD_PWKEY,0);
    gpio_set_level(SIMCARD_RST,1);
    gpio_set_level(SIM800_POWER_ON,1);

    
}