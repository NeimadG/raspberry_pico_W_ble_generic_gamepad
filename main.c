/**
 * Copyright (c) 2026 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "hardware/adc.h"
#include "memflash.h"
#include "acquisition.h"

// This is implemented isn pico-sdk/lib/btstack/example/<example name>.c
int btstack_main(int argc, const char * argv[]);


// This is used in some examples to turn the led on and off
void hal_led_toggle(void){
    static int led_state;
    led_state = 1 - led_state;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
}

int main() {
    stdio_init_all();

    if (cyw43_arch_init() != PICO_OK) {
        panic("failed to cyw43");
    }
    
    // while (!stdio_usb_connected() && get_absolute_time() < 5000000)
    // { // blink the pico's led until usb connection is established
    //     hal_led_toggle();
    //     sleep_ms(250);
    //     hal_led_toggle();
    //     sleep_ms(250);
    // }

    initMem();
    adc_init();
    acquisition_init();

    // Setup the example
    btstack_main(0, NULL);

    btstack_run_loop_execute(); // run until btstack_run_loop_trigger_exit is called

    cyw43_arch_deinit();
    return 0;
}
