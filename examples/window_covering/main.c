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

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define POSITION_OPEN 100
#define POSITION_CLOSED 0
#define POSITION_STATE_CLOSING 0
#define POSITION_STATE_OPENING 1
#define POSITION_STATE_STOPPED 2

// const int gpio_up = 2;
// const int gpio_down = 0;
//Big node mcu
const int gpio_up = 4;
const int gpio_down = 0;
const int on = 0;
const int off = 1;

TaskHandle_t updateStateTask;
TaskHandle_t updateTiltTask;
homekit_characteristic_t current_position;
homekit_characteristic_t target_position;
homekit_characteristic_t position_state;
homekit_characteristic_t target_tilt_angle;
homekit_characteristic_t current_tilt_angle;
homekit_accessory_t *accessories[];

static void wifi_init() {
    struct sdk_station_config wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
    };

    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&wifi_config);
    sdk_wifi_station_connect();
}

void gpio_init() {
    gpio_enable(gpio_up, GPIO_OUTPUT);
    gpio_write(gpio_up, off);
    gpio_enable(gpio_down, GPIO_OUTPUT);
    gpio_write(gpio_down, off);
}

void update_state() {
    bool working = false;
    while (true) {
        int8_t direction = position_state.value.int_value == POSITION_STATE_OPENING ? 1 : -1;
        uint8_t position = current_position.value.int_value;
        int16_t newPosition = position + direction;

        if(working == false && direction == 1) {
            gpio_write(gpio_up, on);
            working = true;
            printf("call up with (%u)\n", target_position.value.int_value);
        } 
        if(working == false && direction == -1) {
            gpio_write(gpio_down, on);
            working = true;
            printf("call down with (%u)\n", target_position.value.int_value);
        }
        current_position.value.int_value = newPosition;
        homekit_characteristic_notify(&current_position, current_position.value);

        if (newPosition == target_position.value.int_value) {
            printf("reached destination %u\n", newPosition);
            if(direction == 1) {
                gpio_write(gpio_up, off);
                working = false;
                printf("stop up with (%u)\n", target_position.value.int_value);
            } 
            if(direction == -1) {
                gpio_write(gpio_down, off);
                working = false;
                printf("stop down with (%u)\n", target_position.value.int_value);
            }
            position_state.value.int_value = POSITION_STATE_STOPPED;
            homekit_characteristic_notify(&position_state, position_state.value);
            vTaskSuspend(updateStateTask);
        }
        // 1 min 6 sec => 66000 ms => 1% === 660 ms
        vTaskDelay(pdMS_TO_TICKS(660));
    }
} 

void update_tilt_angle() {
    bool working = false;
    while (true) {
        int8_t direction = current_tilt_angle.value.int_value < target_tilt_angle.value.int_value ? 1 : -1;
        int8_t position = current_tilt_angle.value.int_value;
        int8_t newTilt = position + direction;

        printf("tilt %d, target titl %d\n", (int)newTilt, target_tilt_angle.value.int_value);
        if(working == false && direction == 1) {
            gpio_write(gpio_up, on);
            working = true;
            printf("call up with (%u)\n", target_tilt_angle.value.int_value);
        } 
        if(working == false && direction == -1) {
            gpio_write(gpio_down, on);
            working = true;
            printf("call down with (%d)\n", target_tilt_angle.value.int_value);
        }
        current_tilt_angle.value.int_value = newTilt;
        homekit_characteristic_notify(&current_tilt_angle, current_tilt_angle.value);

        if (newTilt == target_tilt_angle.value.int_value) {
            printf("reached destination %d\n", newTilt);
            if(direction == 1) {
                gpio_write(gpio_up, off);
                working = false;
                printf("stop up with (%d)\n", target_tilt_angle.value.int_value);
            } 
            if(direction == -1) {
                gpio_write(gpio_down, off);
                working = false;
                printf("stop down with (%d)\n", target_tilt_angle.value.int_value);
            }
            vTaskSuspend(updateTiltTask);
        }
        // 3 sec => 3000 ms => 1° === 33 ms
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void init_tasks() {
    xTaskCreate(update_state, "UpdateState", 256, NULL, tskIDLE_PRIORITY, &updateStateTask);
    vTaskSuspend(updateStateTask);
    xTaskCreate(update_tilt_angle, "UpdateTilt", 256, NULL, tskIDLE_PRIORITY, &updateTiltTask);
    vTaskSuspend(updateTiltTask);
}

void window_covering_identify(homekit_value_t _value) {
    printf("Curtain identify\n");
}

void on_update_target_position(homekit_characteristic_t *ch, homekit_value_t value, void *context);

void on_update_tilt_angle(homekit_characteristic_t *ch, homekit_value_t value, void *context);

homekit_characteristic_t current_position = {
    HOMEKIT_DECLARE_CHARACTERISTIC_CURRENT_POSITION(POSITION_CLOSED)
};

homekit_characteristic_t target_position = {
    HOMEKIT_DECLARE_CHARACTERISTIC_TARGET_POSITION(POSITION_CLOSED, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_target_position))
};

homekit_characteristic_t position_state = {
    HOMEKIT_DECLARE_CHARACTERISTIC_POSITION_STATE(POSITION_STATE_STOPPED)
};

homekit_characteristic_t current_tilt_angle = {
    HOMEKIT_DECLARE_CHARACTERISTIC_CURRENT_HORIZONTAL_TILT_ANGLE(-45)
};

homekit_characteristic_t target_tilt_angle = {
    HOMEKIT_DECLARE_CHARACTERISTIC_TARGET_HORIZONTAL_TILT_ANGLE(-45, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_tilt_angle))
};

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_window_covering, .services=(homekit_service_t*[]) {
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Window blind"),
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "MNK"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "001"),
            HOMEKIT_CHARACTERISTIC(MODEL, "MyCurtain"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, window_covering_identify),
            NULL
        }),
        HOMEKIT_SERVICE(WINDOW_COVERING, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Window blind"),
            &current_position,
            &target_position,
            &position_state,
            &current_tilt_angle,
            &target_tilt_angle,
            NULL
        }),
        NULL
    }),
    NULL
};

void on_update_target_position(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
    if (target_position.value.int_value == current_position.value.int_value) {
        printf("Current position equal to target. Stopping.\n");
        position_state.value.int_value = POSITION_STATE_STOPPED;
        homekit_characteristic_notify(&position_state, position_state.value);
        vTaskSuspend(updateStateTask);
    } else {
        position_state.value.int_value = target_position.value.int_value > current_position.value.int_value
            ? POSITION_STATE_OPENING
            : POSITION_STATE_CLOSING;

        homekit_characteristic_notify(&position_state, position_state.value);
        vTaskResume(updateStateTask);
    }
}

void on_update_tilt_angle(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
    printf("current %d target %d\n", current_tilt_angle.value.int_value, target_tilt_angle.value.int_value);
    if (target_tilt_angle.value.int_value == current_tilt_angle.value.int_value) {
        printf("Current titlt equal to target. Stopping.\n");
        vTaskSuspend(updateTiltTask);
    } else {
        vTaskResume(updateTiltTask);
    }
}

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111"
};

void user_init(void) {
    uart_set_baud(0, 115200);
    wifi_init();
    homekit_server_init(&config);
    init_tasks();
    gpio_init();
    printf("init complete\n");
}
