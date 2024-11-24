/* Light Streamer
Stream light values from the TSL2591 sensor to a MQTT topic.
tim@gremalm.se
http://tim.gremalm.se
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "e131.h"
#include "dmxlight.h"
#include "hibernate.h"

// XIAO ESP32C6 Antenna MUX IO
#define ANT_MUX_PWR_PIN     3   // IO 3
#define ANT_MUX_SEL_PIN     14  // IO 14
#define ANT_MUX_PWR_BIT     BIT(ANT_MUX_PWR_PIN)
#define ANT_MUX_SEL_BIT     BIT(ANT_MUX_SEL_PIN)


// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT	BIT0
#define WIFI_FAIL_BIT		BIT1

static const char *TAG = "Main";
static int s_retry_num = 0;

dmxlight_config_t dmxlightconfig;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		//if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
		//	s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		//} else {
		//	xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		//}
		//ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

void wifi_init_sta(void) {
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
														ESP_EVENT_ANY_ID,
														&event_handler,
														NULL,
														&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
														IP_EVENT_STA_GOT_IP,
														&event_handler,
														NULL,
														&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD,
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,
			.pmf_cfg = {
				.capable = true,
				.required = false
			},
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
			WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
			pdFALSE,
			pdFALSE,
			portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
				 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
				 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	/* The event will not be processed after unregister */
	//ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	//ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	//vEventGroupDelete(s_wifi_event_group);
}

void app_main(void) {
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	//Enable XIAO ESP32C6 antenna mux
	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_INTR_DISABLE;	//disable interrupt
	io_conf.mode = GPIO_MODE_OUTPUT;	//set as output mode
	io_conf.pin_bit_mask = ANT_MUX_PWR_BIT | ANT_MUX_SEL_BIT;  //Configure both pins simultaneously
	io_conf.pull_down_en = 0;	//disable pull-down mode
	io_conf.pull_up_en = 0;		//disable pull-up mode
    ESP_ERROR_CHECK(gpio_config(&io_conf));

	ESP_ERROR_CHECK(gpio_set_level(ANT_MUX_PWR_PIN, 0)); //Enable antenna mux
	ESP_ERROR_CHECK(gpio_set_level(ANT_MUX_SEL_PIN, 0)); //Use internal antenna
	vTaskDelay(100);

	xTaskCreate(&dmxlighttask, "DMX_Light_task", 4096, &dmxlightconfig, 5, NULL);
	
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	wifi_init_sta();

	xTaskCreate(&e131task, "E131_task", 4096, NULL, 5, NULL);

	xTaskCreate(&hibernatetask, "Hibernate_task", 4096, NULL, 5, NULL);
}

