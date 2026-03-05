idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer driver esp_system
                    PRIV_REQUIRES esp_rom)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
