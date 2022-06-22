/* MQTT Mutual Authentication Example */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "dht11.h"
#include "esp_log.h"
#include "mqtt_client.h"
#define GPIO_OUT_0     2
#define GPIO_IN_0      0
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUT_0))
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_IN_0))
#define ESP_INTR_FLAG_DEFAULT 0
#define ON 1
#define OFF 0
#define LED_CHANGE_DELAY_MS    250

#define DHTC11_CHANGE_DELAY_MS  10000

// Set your local broker URI
#define BROKER_URI "mqtts://192.168.0.211:8883"
#define MOSQUITO_USER_NAME              "----"
#define MOSQUITO_USER_PASSWORD          "----"
#define DEVICE_ID "1"

#define DHT_TIME_DELAY_MS      5000

static const char *TAG = "MQTTS_EXAMPLE";
bool mqtt_client_connected = false;
bool mqtt_disconnected_event_flag = false;
char buffer_temperatura[20];
char buffer_humedad[20];
esp_mqtt_client_handle_t client;
extern const uint8_t client_cert_pem_start[] asm("_binary_client_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_client_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_client_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_client_key_end");
extern const uint8_t server_cert_pem_start[] asm("_binary_broker_CA_crt_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_broker_CA_crt_end");
float temperatura = -1000;
float humedad = -1000;
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/mqtt_server/status", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/departamento/e/comedor/planta/regador", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        /*msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);*/
        mqtt_client_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_client_connected = true;
        mqtt_disconnected_event_flag = true;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/departamento/e/comedor/planta/regador/estado", "conectado", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void *parm)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = BROKER_URI,
        .username = MOSQUITO_USER_NAME,
        .password = MOSQUITO_USER_PASSWORD,
        .client_cert_pem = (const char *)client_cert_pem_start,
        .client_key_pem = (const char *)client_key_pem_start,
        .cert_pem = (const char *)server_cert_pem_start,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    while(1){
        while(!mqtt_disconnected_event_flag) vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_mqtt_set_config(client, &mqtt_cfg);
        mqtt_disconnected_event_flag = false;
    }
    vTaskDelete(NULL);
}

static void show_dht_11_info(void* arg)
{
    while(true){
    vTaskDelay(DHT_TIME_DELAY_MS / portTICK_RATE_MS);
    temperatura = DHT11_read().temperature;
    humedad = DHT11_read().humidity;
    if(temperatura != -1 && humedad != -1) {
        sprintf(buffer_temperatura, "%.1f", temperatura);
        sprintf(buffer_humedad, "%.1f", humedad);
    }
    printf("La Humedad registrada es de %.1d\n", DHT11_read().humidity);
    printf("La Temperatura registrada es de %s\n", buffer_temperatura);
    }
}

void publicar_temperatura_task(void * parm)
{
    ESP_LOGI(TAG, "Ingresa a publicar_temperatura_task()");
    char buffer_json[300];
	uint16_t intervalo_publicacion_segundos = 30;
	int msg_id;

	while(1)
	{
        if (mqtt_client_connected) {
            // INICIO CICLO DE LECTURAS y publicaciones.
            vTaskDelay(intervalo_publicacion_segundos * 1000 / portTICK_PERIOD_MS);
            buffer_json[0] = 0;
                strcat(buffer_json, "{\"dispositivo_id\": ");
                strcat(buffer_json, DEVICE_ID);
                strcat(buffer_json, ", \"temperatura\": ");
                strcat(buffer_json, buffer_temperatura);
                strcat(buffer_json, ", \"humedad\": ");
                strcat(buffer_json, buffer_humedad);
                strcat(buffer_json, " }");

            ESP_LOGI("JSON enviado:", " %s ", buffer_json);
            msg_id = esp_mqtt_client_publish(client, "/departamento/e/comedor/planta/regador", buffer_json, 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        }
	}
    vTaskDelete(NULL);
}

void app_main(void)
{
        // GPIO OUTPUTS CONFIG
    //zero-initialize the config structure.
    gpio_config_t out_conf = {};
    //disable interrupt
    out_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    out_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    out_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    out_conf.pull_down_en = 0;
    //disable pull-up mode
    out_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&out_conf);

    // GPIO INPUTS CONFIG
    //zero-initialize the config structure.
    gpio_config_t in_conf = {};
    //disable interrupt
    in_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    in_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    in_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //disable pull-down mode
    in_conf.pull_down_en = 0;
    //enable pull-up mode
    in_conf.pull_up_en = 1;
    //configure GPIO with the given settings
    gpio_config(&in_conf);

    //configure el puerto del sensor
    DHT11_init(GPIO_NUM_23);
    // 5 seg delay
    printf("Waiting 5 sec\n");
    vTaskDelay(5000 / portTICK_RATE_MS);
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    xTaskCreate(show_dht_11_info, "show_dht_11_info", 2048, NULL, 5, NULL);  
    xTaskCreate(mqtt_app_start, "mqtt_app_task", 4096 * 8, NULL, 3, NULL);
    xTaskCreate(publicar_temperatura_task, "temp_pub_task", 4096 * 8, NULL, 3, NULL);
}
