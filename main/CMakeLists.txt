idf_component_register(SRCS "main.cpp" "ble_keyboard.cpp" "dev_link.cpp" "ui_view.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES m5stack__m5unified stackchan_bsp nvs_flash usb bt
                                esp_wifi esp_netif esp_event esp_http_server
                                app_update esp_driver_usb_serial_jtag
                                esp_hid input_router hid_descriptor touchpad
                                espressif__usb_host_hid espressif__mdns)
