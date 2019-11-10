#ifndef _TTGO_H_
#define _TTGO_H_

#include "driver/gpio.h"

#define SIMCARD_RST    (GPIO_NUM_5)
#define SIMCARD_PWKEY  (GPIO_NUM_4)
#define SIM800_POWER_ON (GPIO_NUM_23)

void initTTGoSIM(void);

#endif