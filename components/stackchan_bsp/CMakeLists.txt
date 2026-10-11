file(GLOB_RECURSE BSP_SOURCES "src/*.cpp")
idf_component_register(SRCS ${BSP_SOURCES}
                       INCLUDE_DIRS "src"
                       REQUIRES m5stack__m5unified nvs_flash
                                esp_driver_uart esp_timer)
