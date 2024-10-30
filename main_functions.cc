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



#define GYRO_THRESHOLD 0.01


// Globals, used for compatibility with Arduino-style sketches.
namespace {
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;
uint64_t i = 0;
// 이전 센서 값을 저장할 변수 추가
vector_t prev_va = {0, 0, 0}, prev_vg = {0, 0, 0};


constexpr int kTensorArenaSize = 30000;
uint8_t tensor_arena[kTensorArenaSize];


float scale = 0.01617;
int zero_point = -25;
float model_output[1][4] = {{}}; 


// 전역 변수
vector_t sensor_data_buffer_1[10];  // 버퍼 1
vector_t sensor_data_buffer_2[10];  // 버퍼 2
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


static void transform_accel_gyro(vector_t *v)
{
  float x = v->x;
  float y = v->y;
  float z = v->z;

  v->x = -x;
  v->y = -z;
  v->z = -y;
}

/**
 * Transformation: to get magnetometer aligned
 * @param  {object} s {x,y,z} sensor
 * @return {object}   {x,y,z} transformed
 */
static void transform_mag(vector_t *v)
{
  float x = v->x;
  float y = v->y;
  float z = v->z;

  v->x = -y;
  v->y = z;
  v->z = -x;
}

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
}

#ifdef __cplusplus
extern "C" {
#endif
void ble_store_config_init(void);
#ifdef __cplusplus
}
#endif
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

void notify(float x, float y, float z) {
  memset(gatt_svr_chr_val, 0, sizeof(gatt_svr_chr_val));
  snprintf(gatt_svr_chr_val, sizeof(gatt_svr_chr_val), "Accel x : %2.6f, Accel y : %2.6f, Accel z : %2.6f", x, y, z);
  ble_gatts_chr_updated(gatt_svr_chr_val_handle); // 클라이언트로 알림 전송
}

// notify_buffer 함수 수정: vector_t 배열을 인자로 받도록 수정
void notify_buffer(vector_t buffer[10]) {
    // 버퍼의 데이터를 문자열로 직렬화

    char data[400];  
    memset(data, 0, sizeof(data));
    
    for (int i = 0; i < 10; ++i) {
        char temp[40];
        snprintf(temp, sizeof(temp), "%2.6f,%2.6f,%2.6f;", buffer[i].x, buffer[i].y, buffer[i].z);
        ESP_LOGI("main", "%s", temp);
        size_t remaining_space = sizeof(data) - strlen(data) - 1;
        strncat(data, temp, remaining_space);
    }

    // BLE 특성을 통해 직렬화된 데이터를 한 번에 전송
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
        /*MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                    event->enc_change.status);*/
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        //MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        /*MODLOG_DFLT(INFO, "notify_tx event; conn_handle=%d attr_handle=%d "
                    "status=%d is_indication=%d",
                    event->notify_tx.conn_handle,
                    event->notify_tx.attr_handle,
                    event->notify_tx.status,
                    event->notify_tx.indication);*/
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /*MODLOG_DFLT(INFO, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);*/
        return 0;

    case BLE_GAP_EVENT_MTU:
        /*MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);*/
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

// 자이로 드리프트 보정 함수
void gyro_drift_correction(vector_t* gyro) {
    // 자이로스코프 바이어스 보정 값 적용
    gyro->x -= cal.gyro_bias_offset.x;
    gyro->y -= cal.gyro_bias_offset.y;
    gyro->z -= cal.gyro_bias_offset.z;

    // 자이로스코프 값이 임계값보다 작으면 0으로 고정
    if (fabs(gyro->x) < GYRO_THRESHOLD) gyro->x = 0;
    if (fabs(gyro->y) < GYRO_THRESHOLD) gyro->y = 0;
    if (fabs(gyro->z) < GYRO_THRESHOLD) gyro->z = 0;
}

// 센서 캘리브레이션 함수
void sensor_calibration() {
    vector_t va, vg, vm;

    // 자이로스코프 오프셋 계산 (정지 상태에서 평균 계산)
    float gyro_x_offset = 0, gyro_y_offset = 0, gyro_z_offset = 0;
    for (int i = 0; i < 100; ++i) {
        ESP_ERROR_CHECK(get_accel_gyro_mag(&va, &vg, &vm));
        gyro_x_offset += vg.x;
        gyro_y_offset += vg.y;
        gyro_z_offset += vg.z;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    cal.gyro_bias_offset.x = gyro_x_offset / 100;
    cal.gyro_bias_offset.y = gyro_y_offset / 100;
    cal.gyro_bias_offset.z = gyro_z_offset / 100;

    ESP_LOGI("Calibration", "Gyro offset: x=%f, y=%f, z=%f", 
             cal.gyro_bias_offset.x, cal.gyro_bias_offset.y, cal.gyro_bias_offset.z);
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

        //ESP_LOGI("SensorTask", "accel X: %2.6f, Y: %2.6f, Z: %2.6f", va.x, va.y, va.z);

        // 정확히 10ms마다 실행되도록 설정
        vTaskDelayUntil(&xLastWakeTime, xDelay);
    }
}

// 2. 10개의 데이터를 BLE로 notify하고 모델을 실행하는 태스크 (정확한 100ms 간격)
void ble_and_model_task(void *pvParameter) {

    TickType_t xLastWakeTime = xTaskGetTickCount(); // 현재 시간을 기준으로 설정
    const TickType_t xDelay = pdMS_TO_TICKS(1000);   // 100ms
    ESP_LOGI("BLEModelTask", "BLEModel Task start");

    while (true) { 
        // 10개의 센서 데이터가 모이면 BLE 전송 및 모델 실행
        if (buffer_index >= 10) {
            buffer_index = 0;  // 버퍼 인덱스 초기화
            // 세마포어로 데이터 보호
            xSemaphoreTake(data_semaphore, portMAX_DELAY);

            // active_buffer와 processing_buffer 교체
            vector_t *temp = active_buffer;
            active_buffer = processing_buffer;
            processing_buffer = temp;
            
            xSemaphoreGive(data_semaphore);

            notify_buffer(processing_buffer);  // 데이터를 직렬화하여 BLE로 전송

            ESP_LOGI("BLEModelTask", "BLE notify complete");

            // 모델에 데이터를 입력하여 실행
            int index = 0;
            for (int row = 0; row < 100; ++row) {
                for (int col = 0; col < 3; ++col) {
                    float value = (col == 0) ? processing_buffer[row].x : (col == 1) ? processing_buffer[row].y : processing_buffer[row].z;
                    uint8_t quantized_value = static_cast<int8_t>(value / scale + zero_point);
                    if (row == 0) ESP_LOGI("main", "col = %d, value = %f, quantized_value = %" PRId8, col, value, quantized_value);
                    input->data.int8[index++] = quantized_value;
                    //ESP_LOGI("main", "%f, %d, %f, %ld", scale, zero_point, input->params.scale, input->params.zero_point);
                }
            }

            // 모델 실행
            TfLiteStatus invoke_status = interpreter->Invoke();
            if (invoke_status == kTfLiteOk) {
                if (output != nullptr && output->data.int8 != nullptr) {
                    char output_buffer[256];
                    int pos = 0;

                    // 양자화된 값을 float로 변환하기 위한 scale과 zero_point 사용
                    for (int i = 0; i < 4; i++) {
                        int8_t y_quantized = output->data.int8[i];  // 각 클래스에 대해 양자화된 출력값
                        // 양자화 해제 (Dequantization)
                        ESP_LOGI("ARRAY", "model_output[0][%d] = %d", i, y_quantized);
                        model_output[0][i] = (y_quantized - output->params.zero_point) * output->params.scale;
                        
                        // 변환된 float 값을 버퍼에 저장
                        pos += snprintf(output_buffer + pos, sizeof(output_buffer) - pos, "%2.3f ", model_output[0][i]);
                        if (pos >= sizeof(output_buffer)) {
                            ESP_LOGE("ARRAY", "Buffer overflow");  // 만약 버퍼가 초과되면 오류를 로그로 출력
                            return;
                        }
                    }
                    ESP_LOGI("BLEModelTask", "%f, %ld", output->params.scale, output->params.zero_point);
                    ESP_LOGI("BLEModelTask", "Model output: %s", output_buffer);
                }
            }

            //ESP_LOGI("BLEModelTask", "Model inference complete");

            // 정확히 100ms마다 실행되도록 설정
            
        }
        vTaskDelayUntil(&xLastWakeTime, xDelay);
    }
}


void setup() {
  #ifdef CONFIG_CALIBRATION_MODE
    calibrate_gyro();
    calibrate_accel();
    calibrate_mag();
  #endif

  

  i2c_mpu9250_init(&cal);
  ahrs_init(SAMPLE_FREQ_Hz, 0.8);

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model provided is schema version %d not equal to supported "
                "version %d.", model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Pull in only the operation implementations we need.
  static tflite::MicroMutableOpResolver<9> resolver;

  resolver.AddFullyConnected();
  resolver.AddExpandDims();
  resolver.AddConv2D();
  resolver.AddReshape();
  resolver.AddAdd();
  resolver.AddMaxPool2D();
  resolver.AddMean();
  resolver.AddLogistic();
  resolver.AddSoftmax();

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  // Obtain pointers to the model's input and output tensors.
  input = interpreter->input(0);
  output = interpreter->output(0);

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

