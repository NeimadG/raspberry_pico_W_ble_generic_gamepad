#ifndef ACQUISITION_H_
#define ACQUISITION_H_

#include <stdlib.h>
#include <stdint.h>

extern int8_t acquisition_axis[2];
extern uint8_t acquisition_buttons;

void acquisition_init();
void acquisition_update();


#endif
