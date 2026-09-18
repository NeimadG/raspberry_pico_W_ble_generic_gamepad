
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

void initMem();
void storeTag(uint32_t tag, const uint8_t* data, uint8_t data_size);
void deleteTag(uint32_t tag);
int readTagInfo(uint32_t tag, uint8_t* data, uint8_t data_size);
