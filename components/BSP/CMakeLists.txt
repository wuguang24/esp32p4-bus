set(src_dirs
            LED
            LCD
            MYIIC
            TOUCH
            SDMMC)

set(include_dirs
            LED
            LCD
            MYIIC
            TOUCH
            SDMMC)

set(requires
            driver
            esp_lcd
            esp_common
            esp_timer
            fatfs)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
