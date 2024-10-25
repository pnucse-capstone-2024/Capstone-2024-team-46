#ifndef GATT_SVR_H
#define GATT_SVR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "host/ble_hs.h"

// gatt_svr.c에서 정의된 변수들을 외부에서 사용할 수 있도록 extern으로 선언
extern char gatt_svr_chr_val[350];
extern uint16_t gatt_svr_chr_val_handle;

#ifdef __cplusplus
}
#endif

#endif // GATT_SVR_H
