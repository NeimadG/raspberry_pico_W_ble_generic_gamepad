#include <stdio.h>
#include "acquisition.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

int8_t acquisition_axis[2];
uint8_t acquisition_buttons;

void acquisition_init()
{
    acquisition_axis[0] = 0;
    acquisition_axis[1] = 0;

    acquisition_buttons = 0;

    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_select_input(0);

    
    // Pins 0 -> 7
    for(uint8_t i = 0 ; i < 8 ; ++i)
    {
        gpio_init(i);
        gpio_set_dir(i, false);
        gpio_pull_up(i);
    }

}

void acquisition_update()
{

    acquisition_buttons = (~gpio_get_all()) & 0xF;

    for(uint8_t i = 0 ; i < 2 ; ++i)
    {
        adc_select_input(i);
        int16_t adcVal = (((int16_t)(adc_read())) - 2048) >> 4;
        acquisition_axis[i] = adcVal;
    }

    // printf("Boutons %08b\n", acquisition_buttons);

    //temp 
    acquisition_axis[1] = 0;

}