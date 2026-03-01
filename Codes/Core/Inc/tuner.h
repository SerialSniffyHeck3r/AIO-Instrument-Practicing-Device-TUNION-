// Tuner.h
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Tuner_Init(uint32_t fs_hz);
void Tuner_SetEnabled(uint8_t on);
void Tuner_SetFsTrimPpm(int32_t ppm);

// I2S2/3 half-buffer에서 호출 (u16 words count)
void Tuner_FeedInterleavedI2S24(const uint16_t* rx_u16, uint32_t n_u16);

// 기존 심볼 유지
void notch_tuner_set_fs_trim_ppm(int32_t ppm);
void notch_set_tuner_enabled(uint8_t on);

extern volatile int32_t TunerMeasurement_x10;
extern volatile uint8_t g_tnr_overload;

#ifdef __cplusplus
}
#endif
