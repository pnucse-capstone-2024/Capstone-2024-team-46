idf_component_register(
    SRCS main.cc main_functions.cc constants.cc output_handler.cc model.cc gatt_svr.cc
                "C:/Espressif/frameworks/esp-idf-v5.3/components/bt/host/nimble/nimble/nimble/host/store/config/src/ble_store_config.c"
    REQUIRES spi_flash driver mpu9250 ahrs nvs_flash bt 
    INCLUDE_DIRS "." "C:/Users/kimkj/Desktop/graduate/Anomaly_Detection_Prometheus/managed_components/espressif__esp-nn/include"
)
