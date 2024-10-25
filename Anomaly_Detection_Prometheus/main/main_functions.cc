/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <inttypes.h>
#include <stdio.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "main_functions.h"
#include "model.h"
#include "constants.h"
#include "output_handler.h"
#include "mpu9250.h"  // MPU9250 라이브러리 포함
#include "esp_timer.h"

#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2c.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"

#include "ahrs.h"
#include "calibrate.h"
#include "common.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "bleprph.h"
#include "gatt_svr.h"
#include <vector>

// Globals, used for compatibility with Arduino-style sketches.
namespace {
const tflite::Model* model1 = nullptr;  // 첫 번째 모델 g_model1
const tflite::Model* model2 = nullptr;  // 두 번째 모델 g_model2
tflite::MicroInterpreter* interpreter1 = nullptr;
tflite::MicroInterpreter* interpreter2 = nullptr;
TfLiteTensor* input1 = nullptr;
TfLiteTensor* output1 = nullptr;
TfLiteTensor* input2 = nullptr;
TfLiteTensor* output2 = nullptr;

constexpr int kTensorArenaSize = 50000;
uint8_t tensor_arena[kTensorArenaSize];

float model1_output; 
float model2_output[3] = {{}};  // g_model2의 3개 소프트맥스 출력

// 전역 변수
vector_t sensor_data_buffer_1[100];  // 버퍼 1
vector_t sensor_data_buffer_2[100];  // 버퍼 2
vector_t *active_buffer;            // 현재 데이터를 저장 중인 버퍼
vector_t *processing_buffer;        // 모델 실행에 사용할 버퍼
int buffer_index = 0;               // 현재 버퍼 인덱스
SemaphoreHandle_t data_semaphore;   // 센서 데이터 보호를 위한 세마포어


calibration_t cal = {
  .mag_offset = {.x = 25.183594, .y = 57.519531, .z = -62.648438},
  .mag_scale = {.x = 1.513449, .y = 1.557811, .z = 1.434039},
  .gyro_bias_offset = {.x = 0.303956, .y = -1.049768, .z = -0.403782},
  .accel_offset = {.x = 0.020900, .y = 0.014688, .z = -0.002580},
  .accel_scale_lo = {.x = -0.992052, .y = -0.990010, .z = -1.011147},
  .accel_scale_hi = {.x = 1.013558, .y = 1.011903, .z = 1.019645}};


#if CONFIG_EXAMPLE_EXTENDED_ADV
static uint8_t ext_adv_pattern_1[] = {
    0x02, 0x01, 0x06,
    0x03, 0x03, 0xab, 0xcd,
    0x03, 0x03, 0x18, 0x11,
    0x11, 0X09, 'n', 'i', 'm', 'b', 'l', 'e', '-', 'b', 'l', 'e', 'p', 'r', 'p', 'h', '-', 'e',
};
#endif
static const char *tag = "main";

#if CONFIG_EXAMPLE_RANDOM_ADDR
static uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;
#else
static uint8_t own_addr_type;
#endif
} //end of namespace

#ifdef __cplusplus
extern "C" {
#endif
void ble_store_config_init(void);
#ifdef __cplusplus
}
#endif

static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

void notify_buffer(int model_output) {
    char data[40];  
    memset(data, 0, sizeof(data));
    
    snprintf(data, sizeof(data), "%d;", model_output);
    ESP_LOGI("notify", "result : %d", model_output);
    
    memset(gatt_svr_chr_val, 0, sizeof(gatt_svr_chr_val));
    snprintf(gatt_svr_chr_val, sizeof(gatt_svr_chr_val), "%s", data);
    ble_gatts_chr_updated(gatt_svr_chr_val_handle);
}

    
/**
 * Logs information about a connection to the console.
 */
static void
bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

#if CONFIG_EXAMPLE_EXTENDED_ADV
/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
ext_bleprph_advertise(void)
{
    struct ble_gap_ext_adv_params params;
    struct os_mbuf *data;
    uint8_t instance = 0;
    int rc;

    /* First check if any instance is already active */
    if(ble_gap_ext_adv_active(instance)) {
        return;
    }

    /* use defaults for non-set params */
    memset (&params, 0, sizeof(params));

    /* enable connectable advertising */
    params.connectable = 1;

    /* advertise using random addr */
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;

    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_2M;
    //params.tx_power = 127;
    params.sid = 1;

    params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;

    /* configure instance 0 */
    rc = ble_gap_ext_adv_configure(instance, &params, NULL,
                                   bleprph_gap_event, NULL);
    assert (rc == 0);

    /* in this case only scan response is allowed */

    /* get mbuf for scan rsp data */
    data = os_msys_get_pkthdr(sizeof(ext_adv_pattern_1), 0);
    assert(data);

    /* fill mbuf with scan rsp data */
    rc = os_mbuf_append(data, ext_adv_pattern_1, sizeof(ext_adv_pattern_1));
    assert(rc == 0);

    rc = ble_gap_ext_adv_set_data(instance, data);
    assert (rc == 0);

    /* start advertising */
    rc = ble_gap_ext_adv_start(instance, 0, 0);
    assert (rc == 0);
}
#else
/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    // 1. 명시적으로 배열 선언
    ble_uuid16_t uuids16[1] = {
        BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)
    };

    // 2. 배열의 주소를 fields 구조체에 할당
    fields.uuids16 = uuids16;

    // 3. 배열의 요소 개수를 설정
    fields.num_uuids16 = 1;

    // 4. uuids16 목록이 완전한지 설정
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}
#endif

#if MYNEWT_VAL(BLE_POWER_CONTROL)
static void bleprph_power_control(uint16_t conn_handle)
{
    int rc;

    rc = ble_gap_read_remote_transmit_power_level(conn_handle, 0x01 );  // Attempting on LE 1M phy
    assert (rc == 0);

    rc = ble_gap_set_transmit_power_reporting_enable(conn_handle, 0x1, 0x1);
    assert (rc == 0);
}
#endif

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */

// BLE 연결 파라미터 업데이트 함수
void update_ble_conn_params(uint16_t conn_handle) {
    struct ble_gap_upd_params params = {0};

    // 연결 간격 설정: 100ms (80 * 1.25ms = 100ms)
    params.itvl_min = 0x0050;  // 최소 연결 간격 (80 * 1.25ms = 100ms)
    params.itvl_max = 0x0050;  // 최대 연결 간격 (80 * 1.25ms = 100ms)
    params.latency = 0;        // 슬레이브 레이턴시 (0으로 설정)
    params.supervision_timeout = 0x0200;  // Supervision timeout (500ms)

    int rc = ble_gap_update_params(conn_handle, &params);
    if (rc != 0) {
        ESP_LOGE("BLE", "Failed to update BLE connection parameters, error code: %d", rc);
    } else {
        ESP_LOGI("BLE", "BLE connection parameters updated successfully");
    }
}

static int
bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;
    struct ble_sm_io pkey = {0};
    int key = 0;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        MODLOG_DFLT(INFO, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);
            update_ble_conn_params(event->connect.conn_handle);
        }
        MODLOG_DFLT(INFO, "\n");

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
#if CONFIG_EXAMPLE_EXTENDED_ADV
            ext_bleprph_advertise();
#else
            bleprph_advertise();
#endif
        }

#if MYNEWT_VAL(BLE_POWER_CONTROL)
	bleprph_power_control(event->connect.conn_handle);
#endif
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        bleprph_print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");

        /* Connection terminated; resume advertising. */
#if CONFIG_EXAMPLE_EXTENDED_ADV
        ext_bleprph_advertise();
#else
        bleprph_advertise();
#endif
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        MODLOG_DFLT(INFO, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        // 연결 파라미터가 업데이트된 후 호출되는 이벤트
        if (rc == 0) {
            ESP_LOGI("BLE", "Min interval: %d units (%.2f ms)", desc.conn_itvl, desc.conn_itvl * 1.25);
            ESP_LOGI("BLE", "Max interval: %d units (%.2f ms)", desc.conn_itvl, desc.conn_itvl * 1.25);
        } else {
            ESP_LOGE("BLE", "Failed to retrieve updated connection parameters");
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "advertise complete; reason=%d",
                    event->adv_complete.reason);
#if CONFIG_EXAMPLE_EXTENDED_ADV
        ext_bleprph_advertise();
#else
        bleprph_advertise();
#endif
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        return 0;

    case BLE_GAP_EVENT_MTU:
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(tag, "PASSKEY_ACTION_EVENT started");


        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // This is the passkey to be entered on peer
            ESP_LOGI(tag, "Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            ESP_LOGI(tag, "Passkey on device's display: %" PRIu32 , event->passkey.params.numcmp);
            ESP_LOGI(tag, "Accept or reject the passkey through console in this format -> key Y or key N");
            pkey.action = event->passkey.params.action;
            if (scli_receive_key(&key)) {
                pkey.numcmp_accept = key;
            } else {
                pkey.numcmp_accept = 0;
                ESP_LOGE(tag, "Timeout! Rejecting the key");
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            static uint8_t tem_oob[16] = {0};
            pkey.action = event->passkey.params.action;
            for (int i = 0; i < 16; i++) {
                pkey.oob[i] = tem_oob[i];
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            ESP_LOGI(tag, "Enter the passkey through console in this format-> key 123456");
            pkey.action = event->passkey.params.action;
            if (scli_receive_key(&key)) {
                pkey.passkey = key;
            } else {
                pkey.passkey = 0;
                ESP_LOGE(tag, "Timeout! Passing 0 as the key");
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
        }
        return 0;

    case BLE_GAP_EVENT_AUTHORIZE:
        MODLOG_DFLT(INFO, "authorize event: conn_handle=%d attr_handle=%d is_read=%d",
                    event->authorize.conn_handle,
                    event->authorize.attr_handle,
                    event->authorize.is_read);

        /* The default behaviour for the event is to reject authorize request */
        event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
        return 0;

#if MYNEWT_VAL(BLE_POWER_CONTROL)
    case BLE_GAP_EVENT_TRANSMIT_POWER:
        MODLOG_DFLT(INFO, "Transmit power event : status=%d conn_handle=%d reason=%d "
                           "phy=%d power_level=%x power_level_flag=%d delta=%d",
                     event->transmit_power.status,
                     event->transmit_power.conn_handle,
                     event->transmit_power.reason,
                     event->transmit_power.phy,
                     event->transmit_power.transmit_power_level,
                     event->transmit_power.transmit_power_level_flag,
                     event->transmit_power.delta);
        return 0;

     case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
        MODLOG_DFLT(INFO, "Pathloss threshold event : conn_handle=%d current path loss=%d "
                           "zone_entered =%d",
                     event->pathloss_threshold.conn_handle,
                     event->pathloss_threshold.current_path_loss,
                     event->pathloss_threshold.zone_entered);
        return 0;
#endif
    }

    return 0;
}

static void
bleprph_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

#if CONFIG_EXAMPLE_RANDOM_ADDR
static void
ble_app_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);

    assert(rc == 0);
}
#endif

static void
bleprph_on_sync(void)
{
    int rc;

#if CONFIG_EXAMPLE_RANDOM_ADDR
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();
#endif

    /* Make sure we have proper identity address set (public preferred) */
#if CONFIG_EXAMPLE_RANDOM_ADDR
    rc = ble_hs_util_ensure_addr(1);
#else
    rc = ble_hs_util_ensure_addr(0);
#endif
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    MODLOG_DFLT(INFO, "Device Address: ");
    print_addr(addr_val);
    MODLOG_DFLT(INFO, "\n");
    /* Begin advertising. */
#if CONFIG_EXAMPLE_EXTENDED_ADV
    ext_bleprph_advertise();
#else
    bleprph_advertise();
#endif
}

void bleprph_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}
// The name of this function is important for Arduino compatibility.

// BLE 관련 초기화 함수
void ble_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE("BLE", "Failed to init nimble %d", ret);
        return;
    }

    ble_hs_cfg.reset_cb = bleprph_on_reset;
    ble_hs_cfg.sync_cb = bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;
#ifdef CONFIG_EXAMPLE_BONDING
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
#endif
#ifdef CONFIG_EXAMPLE_MITM
    ble_hs_cfg.sm_mitm = 1;
#endif
#ifdef CONFIG_EXAMPLE_USE_SC
    ble_hs_cfg.sm_sc = 1;
#else
    ble_hs_cfg.sm_sc = 0;
#endif
#ifdef CONFIG_EXAMPLE_RESOLVE_PEER_ADDR
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
#endif

    int rc = gatt_svr_init();
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set("ESP32-Prometheus");
    assert(rc == 0);

    ble_store_config_init();

    nimble_port_freertos_init(bleprph_host_task);
}

// 1. 센서 값을 100Hz로 받아오는 태스크 (정확한 10ms 간격)
void sensor_task(void *pvParameter) {
    ESP_LOGI("SensorTask", "SensorTaskStart");
    TickType_t xLastWakeTime = xTaskGetTickCount(); // 현재 시간을 기준으로 설정
    const TickType_t xDelay = pdMS_TO_TICKS(10);    // 100Hz -> 10ms

    while (true) {
        vector_t va;

        // 센서 데이터 수집
        ESP_ERROR_CHECK(get_accel(&va));

        // 세마포어로 데이터 보호
        xSemaphoreTake(data_semaphore, portMAX_DELAY);
        active_buffer[buffer_index] = va;  // 현재 활성 버퍼에 저장
        buffer_index++;
        xSemaphoreGive(data_semaphore);

        // 정확히 10ms마다 실행되도록 설정
        vTaskDelayUntil(&xLastWakeTime, xDelay);
    }
}

// 2. 100개의 데이터를 BLE로 notify하고 모델을 실행하는 태스크 (정확한 1000ms 간격)
void ble_and_model_task(void *pvParameter) {

    TickType_t xLastWakeTime = xTaskGetTickCount(); // 현재 시간을 기준으로 설정
    const TickType_t xDelay = pdMS_TO_TICKS(1000);   // 1000ms
    ESP_LOGI("BLEModelTask", "BLEModel Task start");
    
    while (true) { 
        // 10개의 센서 데이터가 모이면 BLE 전송 및 모델 실행
        if (buffer_index >= 100) {
            buffer_index = 0;  // 버퍼 인덱스 초기화
            // 세마포어로 데이터 보호
            xSemaphoreTake(data_semaphore, portMAX_DELAY);

            // active_buffer와 processing_buffer 교체
            vector_t *temp = active_buffer;
            active_buffer = processing_buffer;
            processing_buffer = temp;


            xSemaphoreGive(data_semaphore);

            // 모델에 데이터를 입력하여 실행
            int index = 0;
            for (int row = 0; row < 100; ++row) {
                for (int col = 0; col < 3; ++col) {
                    float value = (col == 0) ? processing_buffer[row].x : (col == 1) ? processing_buffer[row].y : processing_buffer[row].z;
                    int8_t quantized_value = static_cast<int8_t>(value / input1->params.scale + input1->params.zero_point);
                    input1->data.int8[index++] = quantized_value;
                }
            }
            
            TfLiteStatus invoke_status1 = interpreter1->Invoke();
            
            // 모델1의 출력을 확인
            if (invoke_status1 == kTfLiteOk && output1 != nullptr && output1->data.int8 != nullptr) {
                int8_t y_quantized = output1->data.int8[0];  
                model1_output = (y_quantized - output1->params.zero_point) * output1->params.scale;

                ESP_LOGI("BLEModelTask", "Model1 output: %f", model1_output * 2);
                
                if (model1_output * 2 < 0.5) {
                    // output이 0.5 미만일 경우 0을 보냄
                    notify_buffer(0);
                    ESP_LOGI("BLEModelTask", "notify");
                } else {
                    // output이 0.5 이상일 경우 model2 실행
                    ESP_LOGI("BLEModelTask", "Running Model2...");

                    // 모델 2에 데이터를 입력하여 실행
                    index = 0;
                    for (int row = 0; row < 100; ++row) {
                        for (int col = 0; col < 3; ++col) {
                            float value = (col == 0) ? processing_buffer[row].x : (col == 1) ? processing_buffer[row].y : processing_buffer[row].z;
                            int8_t quantized_value = static_cast<int8_t>(value / input2->params.scale + input2->params.zero_point);
                            input2->data.int8[index++] = quantized_value;
                        }
                    }

                    TfLiteStatus invoke_status2 = interpreter2->Invoke();
                    if (invoke_status2 == kTfLiteOk && output2 != nullptr && output2->data.int8 != nullptr) {
                        char output_buffer[256];
                        int pos = 0;

                        // 양자화된 값을 float로 변환하기 위한 scale과 zero_point 사용
                        for (int i = 0; i < 3; i++) {
                            int8_t y_quantized = output2->data.int8[i];  // 각 클래스에 대해 양자화된 출력값
                            model2_output[i] = (y_quantized - output2->params.zero_point) * output2->params.scale;
                            pos += snprintf(output_buffer + pos, sizeof(output_buffer) - pos, "%2.3f ", model2_output[i]);
                        }
                        ESP_LOGI("BLEModelTask", "Model2 output: %s", output_buffer);

                        // 가장 높은 확률의 인덱스를 찾아 전송 (1, 2, 3)
                        int max_index = 0;
                        for (int i = 1; i < 3; ++i) {
                            if (model2_output[i] > model2_output[max_index]) {
                                max_index = i;
                            }
                        }
                        notify_buffer(max_index + 1);  // 인덱스 + 1을 전송
                        ESP_LOGI("BLEModelTask", "notify");
                    }
                }
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xDelay);
    }
}

void setup() {
    #ifdef CONFIG_CALIBRATION_MODE
        calibrate_accel();
    #endif

    i2c_mpu9250_init(&cal);
    ahrs_init(SAMPLE_FREQ_Hz, 0.8);

    // 모델 1 초기화
    model1 = tflite::GetModel(g_model1);
    if (model1->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("Model1 provided is schema version %d not equal to supported "
                    "version %d.", model1->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    // 모델 2 초기화
    model2 = tflite::GetModel(g_model2);
    if (model2->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("Model2 provided is schema version %d not equal to supported "
                    "version %d.", model2->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    // Pull in only the operation implementations we need.
    static tflite::MicroMutableOpResolver<9> resolver;

    resolver.AddConv2D();      
    resolver.AddAdd();  
    resolver.AddMaxPool2D(); 
    resolver.AddMean();  
    resolver.AddReshape();  
    resolver.AddSoftmax();  
    resolver.AddExpandDims();
    resolver.AddFullyConnected();
    resolver.AddLogistic();
  

    // 모델 1 인터프리터 빌드
    static tflite::MicroInterpreter static_interpreter1(
        model1, resolver, tensor_arena, kTensorArenaSize / 2);  // 모델 1에 할당할 메모리 크기
    interpreter1 = &static_interpreter1;

    // 모델 2 인터프리터 빌드
    static tflite::MicroInterpreter static_interpreter2(
        model2, resolver, tensor_arena + kTensorArenaSize / 2, kTensorArenaSize / 2);  // 모델 2에 할당할 메모리 크기
    interpreter2 = &static_interpreter2;

    // 모델 1 메모리 할당
    TfLiteStatus allocate_status1 = interpreter1->AllocateTensors();
    if (allocate_status1 != kTfLiteOk) {
        MicroPrintf("AllocateTensors() for Model1 failed");
        return;
    }

    // 모델 2 메모리 할당
    TfLiteStatus allocate_status2 = interpreter2->AllocateTensors();
    if (allocate_status2 != kTfLiteOk) {
        MicroPrintf("AllocateTensors() for Model2 failed");
        return;
    }
    // 모델 1과 2의 입력과 출력 텐서 지정
    input1 = interpreter1->input(0);
    output1 = interpreter1->output(0);
    input2 = interpreter2->input(0);
    output2 = interpreter2->output(0);

    // Keep track of how many inferences we have performed.

    ble_init();
    // 세마포어 초기화
    data_semaphore = xSemaphoreCreateMutex();

    // 버퍼 초기화
    active_buffer = sensor_data_buffer_1;
    processing_buffer = sensor_data_buffer_2;

    xTaskCreate(sensor_task, "SensorTask", 4096, NULL, 5, NULL);
    xTaskCreate(ble_and_model_task, "ModelTask", 4096 * 4, NULL, 5, NULL);

}

