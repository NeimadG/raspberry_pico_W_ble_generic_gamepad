/*
 * Copyright (C) 2017 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "genericPiStick.c"

// *****************************************************************************
/* EXAMPLE_START(genericPiStick): HID Mouse LE
 */
// *****************************************************************************

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "genericPiStick.h"
#include "btstack.h"

#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"
#include "btstack_tlv.h"
#include "ble/sm.h"
#include "memflash.h"
#include "acquisition.h"

typedef struct 
{
    bd_addr_t addr;
    uint8_t cle[16];
    uint8_t typeCle;
} T_INFO_CLE;

T_INFO_CLE cleSecu = {};

static btstack_timer_source_t timerRefresh;
const uint32_t refreshPeriod_ms = 20;

static int custom_tlv_get(void * context, uint32_t tag, uint8_t * buffer, uint32_t buffer_size)
{
    int sizeRead = readTagInfo(tag, buffer, buffer_size);    
    return sizeRead; 
}

static int custom_tlv_store(void * context, uint32_t tag, const uint8_t * data, uint32_t data_size)
{
    storeTag(tag, data, data_size);
    return 0;
}

static void custom_tlv_delete(void *context, uint32_t tag)
{
    deleteTag(tag);
}

// Liaison des fonctions à la structure TLV de BTstack
static const btstack_tlv_t custom_tlv_impl = {
    .get_tag = &custom_tlv_get,
    .store_tag = &custom_tlv_store,
    .delete_tag = &custom_tlv_delete
};


#define REPORT_ID 0x01

// from USB HID Specification 1.1, Appendix B.2
const uint8_t hid_descriptor[] = {
    0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,        // USAGE (Gamepad)
    0xa1, 0x01,        // COLLECTION (Application)

    0x85, REPORT_ID,        //   REPORT_ID (1)
    
    // Boutons (8 boutons)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x08,        //   Usage Maximum (Button 8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit par bouton)
    0x95, 0x08,        //   Report Count (8 boutons = 1 octet au total)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Axes (X, Y) - 1 octet chacun (0-127)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x15, 0x81,        //   Logical Minimum (-127)
    0x25, 0x7F,        //   Logical Maximum (127)
    0x75, 0x08,        //   Report Size (8 bits par axe)
    0x95, 0x02,        //   Report Count (2 axes = 2 octets)
    0x81, 0x02,        //   Input (Data,Var,Abs)
   
    0xC0               // END_COLLECTION
};

static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t l2cap_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;
static uint8_t battery = 100;
static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;


// Fonction de rappel (Callback) appelée à l'échéance du timer
static void timerRefresh_handler(btstack_timer_source_t *ts)
{
    hids_device_request_can_send_now_event(con_handle);
}


void extraireCleSecu(uint8_t * packet, T_INFO_CLE* p_infoCle)
{
    // 1. Extract the 6-byte Bluetooth Address (starts at offset 2)
    reverse_bd_addr(&packet[2], p_infoCle->addr);

    // 2. Extract the 16-byte Link Key (starts at offset 8)
    memcpy(p_infoCle->cle, &packet[8], 16);

    // 3. Extract the Link Key Type (offset 24)
    p_infoCle->typeCle = packet[24];
}

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

const uint8_t adv_data[] = {
    // Flags general discoverable, BR/EDR not supported
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Name
    0x0F, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'G', 'e', 'n', 'e', 'r', 'i', 'c', 'P', 'I', 'S', 't', 'i', 'c', 'k',
    // 16-bit Service UUIDs
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xff, ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    // Appearance HID - Gamepad
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC3, 0x03,
};
const uint8_t adv_data_len = sizeof(adv_data);

static void piStick_setup(void)
{
    btstack_memory_init();
    btstack_tlv_set_instance(&custom_tlv_impl, NULL);

    // setup l2cap and
    l2cap_init();

    // setup SM: Display only
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_MITM_PROTECTION | SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);

    // setup ATT server
    att_server_init(profile_data, NULL, NULL);

    // setup battery service
    battery_service_server_init(battery);

    // setup device information service
    device_information_service_server_init();

    // setup HID Device service
    hids_device_init(0, hid_descriptor, sizeof(hid_descriptor));

    // setup advertisements
    uint16_t adv_int_min = 0x0030;
    uint16_t adv_int_max = 0x0030;
    uint8_t adv_type = 0;
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(adv_data_len, (uint8_t*) adv_data);
    gap_advertisements_enable(1);

    // register for events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // register for connection parameter updates
    l2cap_event_callback_registration.callback = &packet_handler;
    l2cap_add_event_handler(&l2cap_event_callback_registration);

    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    hids_device_register_packet_handler(packet_handler);
}

static void piStick_can_send_now(void)
{
    acquisition_update();
    uint8_t report[] = {acquisition_buttons, (uint8_t) acquisition_axis[0], (uint8_t) acquisition_axis[1] };
    hids_device_send_input_report(con_handle, report, sizeof(report));

    // prepare timer for uptates
    btstack_run_loop_set_timer(&timerRefresh, refreshPeriod_ms);
    btstack_run_loop_add_timer(&timerRefresh);
}

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            con_handle = HCI_CON_HANDLE_INVALID;
            btstack_run_loop_remove_timer(&timerRefresh);
            break;
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;
        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_numeric_comparison_confirm(sm_event_passkey_display_number_get_handle(packet));
            break;
        case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
            break;
        case L2CAP_EVENT_CONNECTION_PARAMETER_UPDATE_RESPONSE:
            break;
        case HCI_EVENT_META_GAP:
            switch (hci_event_gap_meta_get_subevent_code(packet)) {
                case GAP_SUBEVENT_LE_CONNECTION_COMPLETE:
                    break;
                default:
                    break;
            }
            break;
        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    break;
                default:
                    break;
            }
            break;  
        case HCI_EVENT_HIDS_META:
            switch (hci_event_hids_meta_get_subevent_code(packet)){
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
                    con_handle = hids_subevent_input_report_enable_get_con_handle(packet);

                    // start timer for updates
                    btstack_run_loop_set_timer(&timerRefresh, refreshPeriod_ms);
                    btstack_run_loop_add_timer(&timerRefresh);

                    break;

                case HIDS_SUBEVENT_BOOT_KEYBOARD_INPUT_REPORT_ENABLE:
                    con_handle = hids_subevent_boot_keyboard_input_report_enable_get_con_handle(packet);
                    break;
                case HIDS_SUBEVENT_BOOT_MOUSE_INPUT_REPORT_ENABLE:
                    con_handle = hids_subevent_boot_mouse_input_report_enable_get_con_handle(packet);
                    break;
                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    piStick_can_send_now();
                    break;
                default:
                    break;
            }
            break;
            
        default:
            break;
    }
}


int btstack_main(int argc, const char * argv[])
{
    UNUSED(argc);
    UNUSED(argv);

    piStick_setup();

    btstack_run_loop_set_timer_handler(&timerRefresh, &timerRefresh_handler);

    // turn on!
    hci_power_control(HCI_POWER_ON);

    return 0;
}
