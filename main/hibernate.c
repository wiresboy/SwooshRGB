#include "hibernate.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include "driver/rtc_io.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>


/* White channel */
#define SWITCH_SENSE_PIN_LP       (2)  // Sense pin - must be LP (RTC) compatible
#define SWITCH_DRIVE_PIN          (21) // Drive pin - used because I'm using an IO as a constant output. It's silly.

/*
Theory of ops:
Goal is to sleep when switch is on "off" position; operate when in "on" position

The "DRIVE PIN" will be continuously driven low; *even when in deep sleep*.
The "SENSE PIN" will have a weak pullup. When input is measured as "low", the switch is "on". When "high", the switch is off.

*/

static const char *TAG = "Hibernate";

void init_hibernate_io() {


	//Configure input (LP) pin as readable GPIO
	ESP_ERROR_CHECK(rtc_gpio_deinit(SWITCH_SENSE_PIN_LP));

	const gpio_config_t config = {
        .pin_bit_mask = BIT(SWITCH_SENSE_PIN_LP),
        .mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));


	//Configure output drive pin
	ESP_ERROR_CHECK(gpio_hold_dis(SWITCH_DRIVE_PIN));
	const gpio_config_t config_output = {
        .pin_bit_mask = BIT(SWITCH_DRIVE_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&config_output));
	ESP_ERROR_CHECK(gpio_set_level(SWITCH_DRIVE_PIN, 0));

	//Make sure to hold state during hibernate
	ESP_ERROR_CHECK(gpio_hold_en(SWITCH_DRIVE_PIN));
	//gpio_deep_sleep_hold_en(); //Maybe not available on C6?
}

void hibernate() {
	ESP_LOGI(TAG, "Hibernating!");

	ESP_ERROR_CHECK(rtc_gpio_init(SWITCH_SENSE_PIN_LP));
	ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(BIT(SWITCH_SENSE_PIN_LP), ESP_EXT1_WAKEUP_ANY_LOW)); //Wakeup on low state
	ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(SWITCH_SENSE_PIN_LP)); //Configure pullup to drive to sleep state (override with external switch)
	ESP_ERROR_CHECK(rtc_gpio_pullup_en(SWITCH_SENSE_PIN_LP));

	esp_deep_sleep_start();
}


void hibernatetask(void *pvParameters) {
	init_hibernate_io();

	vTaskDelay(5000 / portTICK_PERIOD_MS);

	ESP_LOGI(TAG, "Started watching for 'sleep' switch state");

	while(1) {
		vTaskDelay(100 / portTICK_PERIOD_MS);

		if (gpio_get_level(SWITCH_SENSE_PIN_LP)) { // high = go to sleep
			vTaskDelay(10 / portTICK_PERIOD_MS); //Cheap debounce
			if (gpio_get_level(SWITCH_SENSE_PIN_LP)) {
				hibernate();
			}
		}
		//hibernate();
	}
}