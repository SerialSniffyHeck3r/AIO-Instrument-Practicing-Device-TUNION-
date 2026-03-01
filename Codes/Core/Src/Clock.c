/*
 * Clock.c
 * - main.c 에서 이미 RTC는 초기화된 상태라고 가정
 * - 여기서는 시간 설정 UI만 구현
 */

#include "stm32f4xx_hal.h"
#include "main.h"
#include "LCD16X2.h"
#include "Clock.h"


/* main.c에만 있고 헤더로 안 나온 친구들 직접 재선언 */
typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS
} ButtonEvent;

typedef enum {
    ROTARY_EVENT_NONE = 0,
    ROTARY_EVENT_CW,
    ROTARY_EVENT_CCW
} RotaryEvent;

/* main.c 에 실제로 있는 전역들을 '참조'만 한다 */
extern volatile ButtonEvent buttonEvents[3];
extern volatile RotaryEvent rotaryEvent3;
extern TIM_HandleTypeDef    htim4;
extern int16_t              prev_count4;
extern void Poll_Rotary(TIM_HandleTypeDef *htim, int16_t *prev_count, RotaryEvent *eventFlag);

/* UI 효과음도 main.c / notch.c 에 있던 거 그대로 쓴다 */
extern void notch_ui_button_beep(void);

/* 깜빡이 플래그 – main.c에서 돌아가는 그놈 */
extern uint8_t UITotalBlinkStatus;

/* 이 파일이 진짜 시각 캐시를 가진다 */
volatile uint8_t g_rtc_hour = 0;
volatile uint8_t g_rtc_min  = 0;
volatile uint8_t g_rtc_sec  = 0;


/* main.c 쪽에서 #define MyLCD ... 했는데, 여기서도 필요하니까 없으면 만들어줌 */
#ifndef MyLCD
#define MyLCD LCD16X2_1
#endif

extern RTC_HandleTypeDef hrtc;

/* 백업레지스터에 "난 시간 맞췄다" 도장 찍을 때 쓸 값 */
#define CLOCK_BKP_REG    RTC_BKP_DR1
#define CLOCK_BKP_MAGIC  0xC10C

/* ---- 기존에 쓰던 헬퍼들 ---- */
void Clock_Init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}

uint8_t Clock_IsTimeValid(void)
{
    uint32_t v = HAL_RTCEx_BKUPRead(&hrtc, CLOCK_BKP_REG);
    return (v == CLOCK_BKP_MAGIC) ? 1u : 0u;
}

void Clock_MarkTimeValid(void)
{
    HAL_RTCEx_BKUPWrite(&hrtc, CLOCK_BKP_REG, CLOCK_BKP_MAGIC);
}

/* 실제로 RTC에 박는 함수 */
static void Clock_WriteToRTC(uint8_t hour, uint8_t min, uint8_t sec)
{
    RTC_TimeTypeDef t = {0};

    t.Hours          = hour;
    t.Minutes        = min;
    t.Seconds        = sec;
    t.TimeFormat     = RTC_HOURFORMAT12_AM;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;

    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);

    /* ★ 여기 추가: 우리 캐시도 맞춰둔다 */
    g_rtc_hour = hour;
    g_rtc_min  = min;
    g_rtc_sec  = sec;

    Clock_MarkTimeValid();
}

/* 2자리 숫자를 LCD 문자열에 박는 작은 헬퍼 */
static void put2(char *dst, uint8_t val)
{
    dst[0] = '0' + (val / 10);
    dst[1] = '0' + (val % 10);
}



void Clock_UpdateCache(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;   // 더미로라도 꼭 읽어야 함

    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);  // ← 이 한 줄이 시계를 살린다

    g_rtc_hour = t.Hours;
    g_rtc_min  = t.Minutes;
    g_rtc_sec  = t.Seconds;
}




/*
 * ====== 여기서부터가 네가 말한 “재사용 가능한 시간 설정 UI” ======
 * - 호출하면 여기서 다 끝날 때까지 안 빠져나옴 (blocking)
 * - H → M → S 순서
 * - 로터리로 값 바꾸고, 버튼2 짧게 누르면 다음 항목
 * - 끝까지 가면 RTC에 기록하고 리턴
 */
void Clock_UI_SetTime(void)
{
    // 1. 현재 RTC 값으로 시작
    RTC_TimeTypeDef now;
    HAL_RTC_GetTime(&hrtc, &now, RTC_FORMAT_BIN);

    uint8_t hour  = now.Hours;
    uint8_t min   = now.Minutes;
    uint8_t sec   = now.Seconds;

    uint8_t stage = 0;   // 0 = hour, 1 = minute, 2 = second
    uint8_t done  = 0;

    // LCD 캐시 – 처음엔 일부러 다 다른 값으로 채워서 첫 프레임은 풀로 그림
    char line1_prev[17];
    char line2_prev[17];
    for (int i = 0; i < 16; i++) {
        line1_prev[i] = 0xFF;
        line2_prev[i] = 0xFF;
    }
    line1_prev[16] = 0;
    line2_prev[16] = 0;

    // 입력 찌꺼기 제거
    rotaryEvent3    = ROTARY_EVENT_NONE;
    buttonEvents[2] = BUTTON_EVENT_NONE;

    while (!done)
    {
        /* ========= 입력 처리 ========= */
        // 로터리
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent *)&rotaryEvent3);
        if (rotaryEvent3 == ROTARY_EVENT_CW) {
            if (stage == 0) {
                hour = (hour + 1u) % 24u;
            } else if (stage == 1) {
                min = (min + 1u) % 60u;
            } else {
                sec = (sec + 1u) % 60u;
            }
            rotaryEvent3 = ROTARY_EVENT_NONE;
            notch_ui_button_beep();
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
            if (stage == 0) {
                hour = (hour == 0u) ? 23u : (uint8_t)(hour - 1u);
            } else if (stage == 1) {
                min = (min == 0u) ? 59u : (uint8_t)(min - 1u);
            } else {
                sec = (sec == 0u) ? 59u : (uint8_t)(sec - 1u);
            }
            rotaryEvent3 = ROTARY_EVENT_NONE;
            notch_ui_button_beep();
        }

        // 엔터 → 다음 필드
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[2] = BUTTON_EVENT_NONE;
            notch_ui_button_beep();
            if (stage < 2u) {
                stage++;
                if (stage == 1) {
                    // H → M 으로 들어올 때 분을 0부터
                    if (min > 59) min = 0;
                    else min = 0;      // 그냥 무조건 0부터 하고 싶으면 이 한 줄만 남겨
                } else if (stage == 2) {
                    // M → S 로 들어올 때 초를 0부터
                    if (sec > 59) sec = 0;
                    else sec = 0;
                }
            } else {
                Clock_WriteToRTC(hour, min, sec);
                done = 1;
            }
        }


        /* ========= LCD 버퍼 구성 ========= */
        // 1줄: 현재 설정 중인 항목
        char line1[17] = "                ";
        if (stage == 0) {
            // "SET HOUR"
            line1[0]='S'; line1[1]='E'; line1[2]='T'; line1[3]=' ';
            line1[4]='H'; line1[5]='O'; line1[6]='U'; line1[7]='R';
        } else if (stage == 1) {
            // "SET MINUTES"
            line1[0]='S'; line1[1]='E'; line1[2]='T'; line1[3]=' ';
            line1[4]='M'; line1[5]='I'; line1[6]='N'; line1[7]='U'; line1[8]='T'; line1[9]='E'; line1[10]='S';
        } else {
            // "SET SECOND"
            line1[0]='S'; line1[1]='E'; line1[2]='T'; line1[3]=' ';
            line1[4]='S'; line1[5]='E'; line1[6]='C'; line1[7]='O'; line1[8]='N'; line1[9]='D';
        }
        line1[16] = '\0';

        // 2줄: hh:mm:ss
        char line2[17] = "                ";
        line2[2] = ':';
        line2[5] = ':';

        // --- HOUR 자리 ---
        if (stage == 0) {
            // 시를 지금 편집 중 → 깜빡임: 숫자 ↔ 공백
            if (UITotalBlinkStatus) {
                line2[0] = '0' + (hour / 10u);
                line2[1] = '0' + (hour % 10u);
            } else {
                line2[0] = ' ';
                line2[1] = ' ';
            }
            // 아직 분/초는 안 건드렸으니 --, --
            line2[3] = '-'; line2[4] = '-';
            line2[6] = '-'; line2[7] = '-';
        }
        else if (stage == 1) {
            // 시는 확정값
            line2[0] = '0' + (hour / 10u);
            line2[1] = '0' + (hour % 10u);

            // 분은 깜빡임
            if (UITotalBlinkStatus) {
                line2[3] = '0' + (min / 10u);
                line2[4] = '0' + (min % 10u);
            } else {
                line2[3] = ' ';
                line2[4] = ' ';
            }

            // 초는 아직 안 했으니 --
            line2[6] = '-'; line2[7] = '-';
        }
        else { // stage == 2, seconds
            // 시/분은 확정 표시
            line2[0] = '0' + (hour / 10u);
            line2[1] = '0' + (hour % 10u);
            line2[3] = '0' + (min / 10u);
            line2[4] = '0' + (min % 10u);

            // 초는 깜빡임
            if (UITotalBlinkStatus) {
                line2[6] = '0' + (sec / 10u);
                line2[7] = '0' + (sec % 10u);
            } else {
                line2[6] = ' ';
                line2[7] = ' ';
            }
        }
        line2[16] = '\0';

        /* ========= 바뀐 글자만 찍기 ========= */
        for (int i = 0; i < 16; i++) {
            if (line1[i] != line1_prev[i]) {
                LCD16X2_Set_Cursor(MyLCD, 1, (uint8_t)(i + 1));
                char c[2] = { line1[i], 0 };
                LCD16X2_Write_String(MyLCD, c);
                line1_prev[i] = line1[i];
            }
        }
        for (int i = 0; i < 16; i++) {
            if (line2[i] != line2_prev[i]) {
                LCD16X2_Set_Cursor(MyLCD, 2, (uint8_t)(i + 1));
                char c[2] = { line2[i], 0 };
                LCD16X2_Write_String(MyLCD, c);
                line2_prev[i] = line2[i];
            }
        }

        HAL_Delay(10);
    }
}

