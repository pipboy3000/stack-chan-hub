file(GLOB AVATAR_SOURCES "src/*.cpp")
idf_component_register(SRCS ${AVATAR_SOURCES}
                       INCLUDE_DIRS "src"
                       REQUIRES espressif__arduino-esp32 m5stack__m5unified)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=reorder -Wno-error=switch)
