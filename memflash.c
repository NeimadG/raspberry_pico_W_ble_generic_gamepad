
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"


#define SLOT_ADR_SIZE 6
#define SLOT_KEY_SIZE 16

// tag + addresse + type key + key + CRC
#define SLOT_MEM_SIZE (1 + SLOT_ADR_SIZE + 1 + SLOT_KEY_SIZE + sizeof(uint16_t))
const uint32_t FLASH_MEMORY_SIZE = 1 << 21;
const uint32_t SECTOR_COUNT = FLASH_MEMORY_SIZE / FLASH_SECTOR_SIZE;

const uint32_t SLOT_MAX_COUNT = FLASH_SECTOR_SIZE / SLOT_MEM_SIZE;

static int currentHostOffset = -1;
// tag + tag of deleted element + CRC
#define DELETED_ELEMENT_SIZE (1 + 4 + 2)

static uint32_t nbDataInFlash = 0;

const uint32_t FLASH_TARGET_OFFSET = 500 * FLASH_SECTOR_SIZE; // sector before the last one, since BTStack uses some
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

// custom type of block in memory
typedef enum
{
    e_type_key = 0x55,
    e_type_tag,
    e_type_delElement,
}E_TYPE_BLOC;


#define POLY 0x1021
uint16_t CRC16_init(const uint8_t *data, size_t length, uint16_t initVal)
{
    uint16_t crc = initVal;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        
        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000) 
            {
                crc = (crc << 1) ^ POLY;
            } 
            else 
            {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

uint16_t CRC16(const uint8_t *data, size_t length)
{
    return CRC16_init(data, length, 0xFFFF);
}

// This function will be called when it's safe to call flash_range_erase
static void call_flash_range_erase(void *param) {
    uint32_t offset = (uint32_t)param;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

// This function will be called when it's safe to call flash_range_program
static void call_flash_range_program(void *param) {
    uint32_t offset = ((uintptr_t*)param)[0];
    const uint8_t *data = (const uint8_t *)((uintptr_t*)param)[1];
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
}

bool checkIntegrity()
{
    nbDataInFlash = 0;
    while((flash_target_contents[nbDataInFlash] != 0xFF) && (nbDataInFlash < FLASH_SECTOR_SIZE))
    {
        switch(flash_target_contents[nbDataInFlash])
        {
            case e_type_key:
                currentHostOffset = nbDataInFlash;
                nbDataInFlash += SLOT_MEM_SIZE;
                break;

            case e_type_tag:
                nbDataInFlash += flash_target_contents[nbDataInFlash + 1];
                break;
            
            case e_type_delElement:
                nbDataInFlash += DELETED_ELEMENT_SIZE;
                break;

            default:
                // unknown type, memory might be corrupted
                return false;
                break;
        }


        uint32_t offsetCrc = nbDataInFlash - sizeof(uint16_t);

        uint16_t crc = CRC16(flash_target_contents, offsetCrc);
        uint16_t crcRead;
        memcpy(&crcRead, &flash_target_contents[offsetCrc], sizeof(uint16_t));

        if (crc != crcRead)
        {
            return false;
        }
    }

    return true;
}


void initMem()
{
    if (checkIntegrity() == false)
    {
        // clear flash
        flash_safe_execute(call_flash_range_erase, (void*)FLASH_TARGET_OFFSET, UINT32_MAX);
        nbDataInFlash = 0;
        currentHostOffset = -1;
    }
}

// stock a block. Crc not provided, but space in memory for it
void storeBlock(uint8_t* block, uint32_t sizeOfBlock)
{
    if ((nbDataInFlash + sizeOfBlock) > FLASH_SECTOR_SIZE)
    {
        // memory full
    }
    else
    {
        uint8_t pageFlash[FLASH_PAGE_SIZE];
        memset(pageFlash, 0xFF, FLASH_PAGE_SIZE);

        // page on which start a new block
        uint32_t offsetNextBlock = nbDataInFlash;
        uint32_t offsetPage = offsetNextBlock / FLASH_PAGE_SIZE;
        uint32_t offsetSlotInPage = offsetNextBlock - (offsetPage * FLASH_PAGE_SIZE);
        
        // CRC on data alread in flash + CRC on new block
        uint16_t crc = CRC16(flash_target_contents, offsetNextBlock);
        crc = CRC16_init(block, sizeOfBlock - sizeof(uint16_t), crc);
        memcpy(&block[sizeOfBlock - sizeof(uint16_t)], &crc, sizeof(uint16_t));

        memcpy(pageFlash, &flash_target_contents[offsetPage * FLASH_PAGE_SIZE], FLASH_PAGE_SIZE);

        if ((offsetSlotInPage + sizeOfBlock) > FLASH_PAGE_SIZE)
        {
            // block overlaps on next page

            // insertable size
            uint32_t insertableSize = FLASH_PAGE_SIZE - offsetSlotInPage;

            // copy first section
            memcpy(&pageFlash[offsetSlotInPage], block, insertableSize);
            
            uintptr_t params[] = {FLASH_TARGET_OFFSET + (offsetPage * FLASH_PAGE_SIZE), (uintptr_t)pageFlash};
            flash_safe_execute(call_flash_range_program, params, UINT32_MAX);
            
            // copy next section
            memset(pageFlash, 0xFF, FLASH_PAGE_SIZE);
            memcpy(pageFlash, &block[insertableSize], sizeOfBlock - insertableSize);

            params[0] = FLASH_TARGET_OFFSET + ((offsetPage + 1) * FLASH_PAGE_SIZE);
            params[1] = (uintptr_t)pageFlash;
            flash_safe_execute(call_flash_range_program, params, UINT32_MAX); 
        }
        else
        {
            // copy section
            memcpy(&pageFlash[offsetSlotInPage], block, sizeOfBlock);

            uintptr_t params[] = {FLASH_TARGET_OFFSET + (offsetPage * FLASH_PAGE_SIZE), (uintptr_t)pageFlash};
            flash_safe_execute(call_flash_range_program, params, UINT32_MAX);
        }

        nbDataInFlash += sizeOfBlock;
    }
}

bool storeTag(uint32_t tag, const uint8_t* data, uint8_t dataSize)
{
    // type of key + block size + tag + data size + CRC
    uint32_t sizeOfBlock = 1 + 1 + 4 + dataSize + sizeof(uint16_t);

    const uint32_t maxBlockSize = 32;

    if (sizeOfBlock > maxBlockSize)
    {
        return false;
    }

    uint8_t tagMem[maxBlockSize]; // type + tag + dataSize infos + infos + CRC, 32 supposing enough
    memset(tagMem, 0, maxBlockSize);
    tagMem[0] = e_type_tag;
    tagMem[1] = sizeOfBlock;
    memcpy(&tagMem[2], &tag, sizeof(tag));
    memcpy(&tagMem[6], data, dataSize);

    if ((nbDataInFlash + sizeOfBlock) > FLASH_SECTOR_SIZE)
    {
        flash_safe_execute(call_flash_range_erase, (void*)FLASH_TARGET_OFFSET, UINT32_MAX);
    }

    storeBlock(tagMem, sizeOfBlock);

    return true;
}

void deleteTag(uint32_t tag)
{
    uint8_t slot[DELETED_ELEMENT_SIZE];
    slot[0] = e_type_delElement;
    memcpy(&slot[1], &tag, sizeof(tag));

    if ((nbDataInFlash + DELETED_ELEMENT_SIZE) > FLASH_SECTOR_SIZE)
    {
        flash_safe_execute(call_flash_range_erase, (void*)FLASH_TARGET_OFFSET, UINT32_MAX);
    }
    else // If flash erased, do not store deletion block
    {
        storeBlock(slot, DELETED_ELEMENT_SIZE);
    }
}


int readTagInfo(uint32_t tag, uint8_t* data, uint8_t dataSize)
{
    uint32_t offset = 0;
    int sizeFound = 0;

    while(offset < nbDataInFlash)
    {
        switch(flash_target_contents[offset])
        {
            case e_type_key:
                offset += SLOT_MEM_SIZE;
                break;

            case e_type_tag:
            {
                uint32_t tagMem;
                memcpy(&tagMem, &flash_target_contents[offset + 2], sizeof(tagMem));

                uint32_t infoSize = flash_target_contents[offset + 1] - 8; // total - 1 (key) - 1 (dataSize block) - 4 (tag) - 2 (CRC)

                if ((tag == tagMem) && (infoSize <= dataSize))
                {
                    memcpy(data, &flash_target_contents[offset + 6], infoSize);
                    sizeFound = infoSize;
                }

                offset += flash_target_contents[offset + 1];

            }
                break;
            
            case e_type_delElement:
                uint32_t tagMem;
                memcpy(&tagMem, &flash_target_contents[offset + 1], sizeof(tagMem));

                if (tag == tagMem)
                {
                    sizeFound = 0;
                }

                offset += DELETED_ELEMENT_SIZE;
                break;

            default:
                // unknown block type, stop search
                offset = nbDataInFlash;
                sizeFound = 0;
                break;
        }

    }

    return sizeFound;
}
