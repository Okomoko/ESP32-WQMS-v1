# main/CMakeLists.txt
# Build configuration for main component with SPIFFS image generation

idf_component_register(SRCS
    # Core
    main.c
    
    # Utils
    utils/nvs_config.c
    utils/watchdog.c
    utils/spiffs_manager.c
    utils/wifi_manager.c
    
    # Logging
    logging/logger.c
    logging/log_rotate.c
    
    # Sensors
    sensor/sensor_read.c
    sensor/sensor_calib.c
    sensor/sensor_history.c
    drivers/adc_dma.c
    drivers/dht11.c
    
    # Relays
    relay/relay_control.c
    
    # NTP
    ntp/ntp_client.c
    
    # MODBUS (Slave only)
    modbus/modbus_tcp.c
    
    # Automation
    automation/rule_manager.c
    automation/automation_engine.c
    
    # Web Server
    webserver/http_server.c
    webserver/api_handler.c
    webserver/api_config.c
    webserver/web_console.c
    
    # Integration
    integration/web_upload.c
    integration/buffer_recovery.c
    integration/web_upload_task.c

    # Email notification
    utils/email_client.c
    
    INCLUDE_DIRS
    "include"
    
    EMBED_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/index.html
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/automation.html
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/config.html
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/calibration.html
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/api_doc.html
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/console.html
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/style.css
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/core.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/chart.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/dashboard.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/automation.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/config.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/calibration.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/console.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/api_docs.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/js/router.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/favicon.ico

    EMBED_TXTFILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../web_assets.min/r1.pem
)

# ============================================================
# SPIFFS Image Generation (for logs and sensors)
# ============================================================
# Note: web_assets is NOT a SPIFFS partition anymore - it's embedded in firmware

# Required components
set(COMPONENT_REQUIRES
    freertos
    esp_system
    esp_common
    esp_rom
    esp_hw_support
    driver
    esp_timer
    esp_netif
    esp_event
    esp_wifi
    esp_base64
    esp_tls
    esp_driver_tsens
    nvs_flash
    spi_flash
    spiffs
    mbedtls
    lwip
    http_server
    vfs
    json
)