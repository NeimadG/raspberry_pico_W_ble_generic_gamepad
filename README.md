# raspberry_pico_W_ble_generic_gamepad
A generic gamepad (game controller), on BLE. Developped in C.

Tested OK on Windows 11, Android 14, MetaQuest 3.

Project is developped with VsCode raspberry pico extension (version 0.23.0). Version of SDK used is 2.3.0.

Buttons are read on GPIO0 to GPIO7 : 0V (GND) means button pressed. Analog inputs on GPIO26 & GPIO27.
Refresh rate is about 50Hz.

## Bluetooth data storage.
Since a defect is reported on BTStack when using mulitcore, a custom data storage is used. It stores data in a sector in flash. The sector is erased only when it is full or corrputed.
