#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <stdint.h>

extern volatile uint8_t g_rtc_hour;
extern volatile uint8_t g_rtc_min;
extern volatile uint8_t g_rtc_sec;

void    Clock_Init(void);
uint8_t Clock_IsTimeValid(void);
void    Clock_MarkTimeValid(void);
void    Clock_SetHMS(uint8_t hour, uint8_t min, uint8_t sec);
void    Clock_UpdateCache(void);

#endif
