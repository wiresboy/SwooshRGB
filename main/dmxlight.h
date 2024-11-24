#ifndef DMXLIGHT_H_
#define DMXLIGHT_H_

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
	QueueHandle_t queue_ui_mode;
	QueueHandle_t queue_ui_pot1;
	QueueHandle_t queue_ui_pot2;
} dmxlight_config_t;

void dmxlighttask(void *pvParameters);
void updateOutput( uint32_t duty_red, uint32_t duty_green, uint32_t duty_blue, uint32_t duty_white);

#endif /* DMXLIGHT_H_ */
