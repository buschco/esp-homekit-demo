#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_system.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <etstimer.h>
#include <esplibs/libmain.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"

#include <dht/dht.h>

#define VALVE_TYPE 1

#define REMAINING_DURATION 0
#define SET_DURATION 10
#define ACTIVE 0
#define IN_USE 0

void update_running(bool);
void update_gate_state(int, bool);
bool is_running;

homekit_accessory_t *accessories[];
TaskHandle_t running_task;

homekit_characteristic_t remaining_duration; // uint32
homekit_characteristic_t set_duration; // uint32
homekit_characteristic_t active; // uint8
homekit_characteristic_t in_use; // uint8

homekit_value_t get_remaining_duration();
homekit_value_t get_active();
homekit_value_t get_in_use();
homekit_value_t get_set_duration();

void set_active(homekit_value_t value);
void set_in_use(homekit_value_t value);
void set_set_duration(homekit_value_t value);
void set_remaining_duration(homekit_value_t value);

void on_update_in_use(homekit_characteristic_t *ch, homekit_value_t value, void *context);
void on_update_active(homekit_characteristic_t *ch, homekit_value_t value, void *context);
void on_update_set_duration(homekit_characteristic_t *ch, homekit_value_t value, void *context);
void on_update_remaining_duration(homekit_characteristic_t *ch, homekit_value_t value, void *context);

void task_fn() {
    while(true) {
        int new_duration = remaining_duration.value.int_value - 1;
        set_remaining_duration(HOMEKIT_UINT32(new_duration));
        vTaskDelay(pdMS_TO_TICKS(1000));
        if(new_duration < 1) {
            update_running(false);
        }
    }
}

void init_running_task() {
    is_running = false;
    xTaskCreate(task_fn, "RunningTask", 256, NULL, tskIDLE_PRIORITY, &running_task);
    vTaskSuspend(running_task);
}

void update_running(bool target) {
    if(target == true) {
        set_in_use(HOMEKIT_UINT8(1)); 
        update_gate_state(1, true);
        set_remaining_duration(set_duration.value);
        vTaskResume(running_task);
    } else {
        update_gate_state(1, false);
        set_in_use(HOMEKIT_UINT8(0)); 
        set_active(HOMEKIT_UINT8(0));
        vTaskSuspend(running_task);
    }
}

/* in_use */
    homekit_characteristic_t in_use = {
        HOMEKIT_DECLARE_CHARACTERISTIC_IN_USE(
            IN_USE,
            .getter=get_in_use,
            .setter=set_in_use,
            .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_in_use),
        )
    };

    homekit_value_t get_in_use() {
        return in_use.value;
    }

    void set_in_use(homekit_value_t value) {
        homekit_characteristic_notify(&in_use, value);
        in_use.value = value;
    }

    void on_update_in_use(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
        printf("on_update_in_use: %d\n", in_use.value.int_value);
    }

/* active */
    homekit_characteristic_t active = {
        HOMEKIT_DECLARE_CHARACTERISTIC_ACTIVE(
            ACTIVE,
            .getter=get_active,
            .setter=set_active,
            .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_active),
        )
    };

    homekit_value_t get_active() {
        return active.value;
    }

    void set_active(homekit_value_t value) {
        active.value = value;
        homekit_characteristic_notify(&active, value);
        if(value.int_value == 1) {
            update_running(true);
        }
    }

    void on_update_active(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
        printf("on_update_active: active: %d in_use: %d\n", active.value.int_value, in_use.value.int_value);
    }

/* remaining_duration */\
    homekit_characteristic_t remaining_duration = {
        HOMEKIT_DECLARE_CHARACTERISTIC_REMAINING_DURATION(
            REMAINING_DURATION,
            .getter=get_remaining_duration,
            .setter=set_remaining_duration,
            .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_remaining_duration),
        )
    };

    homekit_value_t get_remaining_duration() {
        return remaining_duration.value;
    }

    void set_remaining_duration(homekit_value_t value) {
        homekit_characteristic_notify(&remaining_duration, value);
        remaining_duration.value = value;
    }

    void on_update_remaining_duration(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
        printf("remaining_duration: %d\n", value.int_value);
    }

/* set_duration */
    homekit_characteristic_t set_duration = {
        HOMEKIT_DECLARE_CHARACTERISTIC_SET_DURATION(
            SET_DURATION,
            .getter=get_set_duration,
            .setter=set_set_duration,
            .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_set_duration)
        )
    };

    homekit_value_t get_set_duration() {
        return set_duration.value;
    }

    void set_set_duration(homekit_value_t value) {
        set_duration.value = value;
    }

    void on_update_set_duration(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
        printf("set_duration: %d\n", set_duration.value.int_value);
    }

/*******/

/* relay controllers */
void update_gate_state(int gate_number, bool state) {
    if(state) {
        printf("--> open gate number: %d \n", gate_number);
    }
    else { 
        printf("--> close gate number: %d \n", gate_number);
    }
}

void valve_identify(homekit_value_t value) {
    printf("Valve identify\n");
}

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_faucet, .services=(homekit_service_t*[]) {
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Valve"),
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "Colin "),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "001"),
            HOMEKIT_CHARACTERISTIC(MODEL, "Valve1"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, valve_identify),
            NULL
        }),
        HOMEKIT_SERVICE(VALVE, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(VALVE_TYPE, 1),
            HOMEKIT_CHARACTERISTIC(NAME, "Valve 1"),
            HOMEKIT_CHARACTERISTIC(STATUS_FAULT, 0),
            HOMEKIT_CHARACTERISTIC(IS_CONFIGURED, 1),
            &active,
            &in_use,
            &remaining_duration,
            &set_duration,
            NULL
        }),
        NULL
    }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111"
};

static void wifi_init() {
    struct sdk_station_config wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
    };

    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&wifi_config);
    sdk_wifi_station_connect();
}

void user_init(void) {
    uart_set_baud(0, 115200);
    wifi_init();
    homekit_server_init(&config);
    init_running_task();
    printf("init complete\n");
}
