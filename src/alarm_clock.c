#include "alarm_clock.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

#include "fonts.h"

//Graphics
extern void cls(uint16_t colour);
extern void flip_frame(void);
extern void gprintf(const char *fmt, ...);
extern int get_orientation(void);
extern void set_orientation(int o);
typedef enum { NONE=0, LEFT_DOWN=1, RIGHT_DOWN=2, UP_DOWN=3, DOWN_DOWN=4, SELECT_DOWN=5 } key_type;
extern key_type get_input(void);
//Colours
#define ALARM_GPIO 25
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_SSID "MasseyWifi"
static const char *TAG = "ALARM";

static EventGroupHandle_t wifi_evt_group;

//Alarm state 
static bool alarm_set = false;
static bool alarm_ring = false;
static time_t alarm_epoch = 0;

//wifi connexn
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(wifi_evt_group, WIFI_CONNECTED_BIT);
}

static void wifi_connect(void) {
    wifi_evt_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, NULL));

    wifi_config_t c = {0};
    strcpy((char *)c.sta.ssid, WIFI_SSID);
    c.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &c));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(wifi_evt_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

//Sntp sync for time
static void sntp_cb(struct timeval *tv) { ESP_LOGI(TAG, "SNTP sync"); }

static void time_sync(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_cb);
    esp_sntp_init();

    time_t now = 0;
    struct tm tmn = {0};
    while (tmn.tm_year < (2016 - 1900)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        localtime_r(&now, &tmn);
    }
}

//helper convert struct tm in UTC to time_t
//mktime assumes tm is local time
static time_t timegm_compat(struct tm *t_utc) {
    time_t local_epoch = mktime(t_utc);
    time_t now = time(NULL);
    struct tm lnow, gnow;
    localtime_r(&now, &lnow);
    gmtime_r(&now, &gnow);
    time_t local_now = mktime(&lnow); 
    time_t utc_now   = mktime(&gnow);
    time_t offset = local_now - utc_now;

    return local_epoch - offset; 
}

//MQTT client setup
static esp_mqtt_client_handle_t client = NULL;

static bool parse_alarm(const char *s, time_t *out) {
    if (!s || strlen(s) < 16) return false;
    int Y, M, D, h, m;
    if (sscanf(s, "%4d-%2d-%2d %2d:%2d", &Y, &M, &D, &h, &m) != 5) return false;

    struct tm t = {0};
    t.tm_year = Y - 1900;
    t.tm_mon  = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min  = m;
    t.tm_sec  = 0;

    time_t e = timegm_compat(&t);
    if (e == (time_t)-1) return false;
    *out = e;
    return true;
}

static void mqtt_evt(void *h, esp_event_base_t b, int32_t id, void *d) {
    esp_mqtt_event_handle_t e = d;
    if (id == MQTT_EVENT_CONNECTED) {
        esp_mqtt_client_subscribe(e->client, "/topic/a159236/alarm", 1);
    } else if (id == MQTT_EVENT_DATA) {
        char buf[64] = {0};
        int n = e->data_len;
        if (n > 63) n = 63;
        memcpy(buf, e->data, n);
        time_t t = 0;
        if (parse_alarm(buf, &t)) {
            alarm_set = true;
            alarm_ring = false;
            alarm_epoch = t;
            ESP_LOGI(TAG, "Alarm set %s", buf);
        } else {
            ESP_LOGW(TAG, "Bad Payload %s", buf);
        }
    }
}

static void mqtt_start(void) {
    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = "mqtt://mqtt.webhop.org";

    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_evt, NULL);
    esp_mqtt_client_start(client);
}

//Display
static void draw_time(const struct tm *now, const struct tm *alarm, bool set) {
    cls(0);
    setFont(FONT_DEJAVU18);
    setFontColour(0, 255, 0);
    int hr12 = now->tm_hour % 12;
    if (hr12 == 0) hr12 = 12;
    gprintf("%02d:%02d:%02d %s\n", hr12, now->tm_min, now->tm_sec, (now->tm_hour >= 12 ? "PM" : "AM"));
    setFontColour(255, 255, 255);
    if (set)
        gprintf("ALARM: %04d-%02d-%02d %02d:%02d UTC\n",
                alarm->tm_year + 1900, alarm->tm_mon + 1, alarm->tm_mday, alarm->tm_hour, alarm->tm_min);
    else
        gprintf("ALARM: --\n");
    flip_frame();
}

static void flash_screen(bool red) {
    cls(red ? 0xF800 : 0x07E0);
    flip_frame();
}

//GPIO init for alarm output
static void alarm_gpio_init(void) {
    gpio_config_t io = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << ALARM_GPIO)};
    gpio_config(&io);
    gpio_set_level(ALARM_GPIO, 0);
}

//main func
void alarm_clock(void) {
    if (get_orientation() != 1) set_orientation(1);

    static bool init = false;
    if (!init) {
        ESP_ERROR_CHECK(nvs_flash_init());
        wifi_connect();
        time_sync();
        mqtt_start();
        alarm_gpio_init();
        init = true;
    }

    bool flash = false;
    for (;;) {
        time_t now;
        time(&now);
        struct tm nt;
        localtime_r(&now, &nt);
        struct tm at = {0};
        if (alarm_set) gmtime_r(&alarm_epoch, &at);
        if (!alarm_ring)
            draw_time(&nt, &at, alarm_set);
        else {
            flash = !flash;
            flash_screen(flash);
            gpio_set_level(ALARM_GPIO, 1);
        }

        if (alarm_set && !alarm_ring && now >= alarm_epoch) alarm_ring = true;

        key_type k = get_input();
        if (alarm_ring && (k == LEFT_DOWN || k == RIGHT_DOWN || k == SELECT_DOWN)) {
            alarm_ring = false;
            alarm_set = false;
            gpio_set_level(ALARM_GPIO, 0);
        }
        if (k == UP_DOWN) {
            alarm_ring = false;
            gpio_set_level(ALARM_GPIO, 0);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(alarm_ring ? 300 : 1000));
    }
}
