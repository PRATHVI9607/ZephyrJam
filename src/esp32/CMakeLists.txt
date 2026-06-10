cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(jamshield C)

target_sources(app PRIVATE
    src/main.c
    src/sensor_ldr.c
    src/jam_detect.c
    src/adaptive_threshold.c
    src/wifi_mqtt.c
    src/ble_gatt.c
    src/espnow_l2.c
    src/conn_mgr_setup.c
    src/payload_thread.c
)

target_include_directories(app PRIVATE include)
