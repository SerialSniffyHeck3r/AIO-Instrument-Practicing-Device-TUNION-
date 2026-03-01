#ifndef BATTERYTEMP_H
#define BATTERYTEMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BatteryTemp: popup renderer for Temperature / Humidity / Battery on Home (ModeSelection)
 *
 * Data sources (extern, provided by main.c):
 *   - uint16_t MainBattADC;   // 12-bit ADC raw (0..4095)
 *   - uint16_t Temperature;   // tenths of °C (e.g., 253 => 25.3°C)
 *   - uint16_t Humidity;      // 0..99 %RH (integer percentage)
 *
 * For now, caller may fix those to constants. This module converts / formats / displays.
 */

typedef struct {
    uint16_t vref_mv;     // e.g., 3300
    float    div_ratio;   // voltage divider ratio from ADC pin to pack ( (Rtop+Rbottom)/Rbottom )
    uint8_t  battery_type;// 0=Alkaline, 1=NiMH, 2=Li-ion(1S nominal). Used for percentage mapping
    uint8_t  cell_count;  // number of series cells (typically 1)
} BatteryTempConfig;

/** Initialize module with conversion parameters. */
void BatteryTemp_Init(const BatteryTempConfig *cfg);

/** Convert 12-bit ADC raw to pack millivolts using vref & divider ratio. */
uint16_t BatteryTemp_BattmV_FromADC(uint16_t adc_raw);

/** Compute a coarse battery % from pack millivolts (linear OCV map by type). */
uint8_t BatteryTemp_BattPercent(uint16_t pack_mv);

/** Format two LCD 16x2 lines. Buffers must be at least 17 bytes (16 chars + NUL). */
void BatteryTemp_Format(char line1[17], char line2[17]);

/** If LCD16X2 is present in the project, render the popup immediately. */
void BatteryTemp_ShowPopupLCD(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERYTEMP_H */
