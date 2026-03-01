#ifndef _DHT_H
#define _DHT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* ★ 추가: HAL 타입/매크로 선언을 dht.h가 직접 보장 */
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_tim.h"

/* 이하 기존 내용 유지 (enum에 DHT22 이미 존재 확인됨) */
typedef enum {
  DHT_Type_DHT11 = 0,
  DHT_Type_DHT12,
  DHT_Type_DHT21,
  DHT_Type_DHT22,   // DHT22 OK
  DHT_Type_AM2301,
  DHT_Type_AM2305
} DHT_Type_t;

typedef struct {
  TIM_HandleTypeDef   *tim;
  GPIO_TypeDef        *gpio;
  uint16_t             pin;
  DHT_Type_t           type;
  uint8_t              data[84];
  uint16_t             cnt;
  uint32_t             time;
  uint32_t             lastCNT;
  float                temperature;
  float                humidity;
  bool                 dataValid;
} DHT_t;

void DHT_pinChangeCallBack(DHT_t *dht);
void DHT_init(DHT_t *dht, DHT_Type_t type, TIM_HandleTypeDef *tim,
              uint16_t timerBusFrequencyMHz, GPIO_TypeDef *gpio, uint16_t pin);
bool DHT_readData(DHT_t *dht, float *Temperature, float *Humidity);

#ifdef __cplusplus
}
#endif
#endif
