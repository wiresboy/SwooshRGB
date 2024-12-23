#include "dmxlight.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "driver/ledc.h"
#include "esp_err.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "e131.h"

/* Channel PWM parameters */
#define LEDC_MODE               LEDC_LOW_SPEED_MODE //HIGH SPEED not available?
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz.
#define LEDC_TIMER          	LEDC_TIMER_0

/* Red channel */
#define LEDC_OUTPUT_IO_RED      (1) // Define the output GPIO 1
#define LEDC_CHANNEL_RED        LEDC_CHANNEL_0
#define LEDC_INVERT_RED         true

/* Green channel */
#define LEDC_OUTPUT_IO_GREEN      (18) // Define the output GPIO 10
#define LEDC_CHANNEL_GREEN        LEDC_CHANNEL_1
#define LEDC_INVERT_GREEN         true

/* Blue channel */
#define LEDC_OUTPUT_IO_BLUE      (20) // Define the output GPIO 9
#define LEDC_CHANNEL_BLUE        LEDC_CHANNEL_2
#define LEDC_INVERT_BLUE         true

/* White channel */
#define LEDC_OUTPUT_IO_WHITE      (15) // Define the output GPIO integrated LED
#define LEDC_CHANNEL_WHITE        LEDC_CHANNEL_3
#define LEDC_INVERT_WHITE         true

#define DMX_8Bit_VAL(offset)      (e131packet.property_values[CONFIG_SACN_DMX_START + (offset)])
#define DMX_16Bit_VAL(offset)     (DMX_8Bit_VAL(offset) * 256 + DMX_8Bit_VAL(offset+1))

static const char *TAG = "DMX Light";
dmxlight_config_t dmxlight_config;

uint32_t duty_max = 8191;  // 13 bit timer (2**13) - 1

void init_led_pwm() {
	// Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC Red PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_RED,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO_RED,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Prepare and then apply the LEDC Green PWM channel configuration
	ledc_channel.channel = LEDC_CHANNEL_GREEN;
	ledc_channel.gpio_num = LEDC_OUTPUT_IO_GREEN;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Prepare and then apply the LEDC Blue PWM channel configuration
	ledc_channel.channel = LEDC_CHANNEL_BLUE;
	ledc_channel.gpio_num = LEDC_OUTPUT_IO_BLUE;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Prepare and then apply the LEDC White PWM channel configuration
	ledc_channel.channel = LEDC_CHANNEL_WHITE;
	ledc_channel.gpio_num = LEDC_OUTPUT_IO_WHITE;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_RED, 0));
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_GREEN, 0));
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_BLUE, 0));
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_WHITE, 0));
}

void dmxlighttask(void *pvParameters) {
	dmxlight_config = *(dmxlight_config_t *) pvParameters;

	ESP_LOGI(TAG, "Init PWM");
	init_led_pwm();

	ESP_LOGI(TAG, "Start - flash light R,G,B");
	updateOutput(duty_max, 0, 0, 0); // R
	vTaskDelay(300 / portTICK_PERIOD_MS);

	updateOutput(0, duty_max, 0, 0); // G
	vTaskDelay(300 / portTICK_PERIOD_MS);

	updateOutput(0, 0, duty_max, 0); // B
	vTaskDelay(300 / portTICK_PERIOD_MS);
	
	updateOutput(0, 0, 0, duty_max); // (W)
	vTaskDelay(300 / portTICK_PERIOD_MS);
	
	updateOutput(0, 0, 0, 0);

	ESP_LOGI(TAG, "Start reading DMX values");
	float dmx_dimmer = 0;
	float dmx_red = 0;
	float dmx_green = 0;
	float dmx_blue = 0;
	float dmx_white = 0;

	uint32_t duty_red = 0;
	uint32_t duty_green = 0;
	uint32_t duty_blue = 0;
	uint32_t duty_white = 0;
	while(1) {
		if (e131packet_received == 0) {
			duty_red = 0;
			duty_green = 0;
			duty_blue = 0;
			duty_white = 0;
		} else {
			/* Read DMX values for start-channel */
			dmx_dimmer = ((float)DMX_16Bit_VAL(0)) / 65535;
			dmx_red    = ((float)DMX_16Bit_VAL(2)) / 65535;
			dmx_green  = ((float)DMX_16Bit_VAL(4)) / 65535;
			dmx_blue   = ((float)DMX_16Bit_VAL(6)) / 65535;
			dmx_white  = ((float)DMX_16Bit_VAL(8)) / 65535;
			// ESP_LOGI(TAG, "Dimmer %f R %f G %f B %f W %f", dmx_dimmer, dmx_red, dmx_green, dmx_blue, dmx_white);

			/* Upscale values to 13 bit, also apply master dimmer channel */
			duty_red = (uint32_t)(((float)duty_max) * dmx_dimmer * dmx_red);
			duty_green = (uint32_t)(((float)duty_max) * dmx_dimmer * dmx_green);
			duty_blue = (uint32_t)(((float)duty_max) * dmx_dimmer * dmx_blue);
			duty_white = (uint32_t)(((float)duty_max) * dmx_dimmer * dmx_white);

		}

		/* Set duty cycle to RGBW values */
		updateOutput(duty_red, duty_green, duty_blue, duty_white);
		vTaskDelay(20 / portTICK_PERIOD_MS);
	}
}

void updateOutput( uint32_t duty_red, uint32_t duty_green, uint32_t duty_blue, uint32_t duty_white) {
	if (LEDC_INVERT_RED)
		duty_red = duty_max - duty_red;
	if (LEDC_INVERT_GREEN)
		duty_green = duty_max - duty_green;
	if (LEDC_INVERT_BLUE)
		duty_blue = duty_max - duty_blue;
	if (LEDC_INVERT_WHITE)
		duty_white = duty_max - duty_white;

	/* Set duty cycle to RGBW values */
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_RED, duty_red));
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_GREEN, duty_green));
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_BLUE, duty_blue));
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_WHITE, duty_white));

	ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_RED));
	ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_GREEN));
	ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_BLUE));
	ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_WHITE));
}