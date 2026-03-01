/*
 * File: LCD16X2.c
 * Driver Name: [[ LCD16x2 Display (GPIO 4-Bit Mode) ]]
 * SW Layer:   ECUAL
 * Created on: Jun 28, 2020
 * OG Author:     Khaled Magdy
 *
 * -------------------------------------------
 * For More Information, Tutorials, etc.
 * Visit Website: www.DeepBlueMbedded.com
 *
 * 2025 Added features
 *
 */

#include "LCD16X2.h"
#include "LCD16X2_cfg.h"
#include "Util.h"

#include <string.h>
#include <stdlib.h>

#ifndef LCD16X2_ANIM_H
#define LCD16X2_ANIM_H
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

//=========================
typedef struct
{
  uint8_t DisplayControl;
  uint8_t DisplayFunction;
  uint8_t DisplayMode;
  uint8_t Rows;
  uint8_t Cols;
  uint8_t currentX;
  uint8_t currentY;
} HD44780_Options_t;
//==========================



#define HD44780_CLEARDISPLAY        0x01
#define HD44780_RETURNHOME          0x02
#define HD44780_ENTRYMODESET        0x04
#define HD44780_DISPLAYCONTROL      0x08
#define HD44780_CURSORSHIFT         0x10
#define HD44780_FUNCTIONSET         0x20
#define HD44780_SETCGRAMADDR        0x40
#define HD44780_SETDDRAMADDR        0x80

/* Flags for display entry mode */
#define HD44780_ENTRYRIGHT          0x00
#define HD44780_ENTRYLEFT           0x02
#define HD44780_ENTRYSHIFTINCREMENT 0x01
#define HD44780_ENTRYSHIFTDECREMENT 0x00

/* Flags for display on/off control */
#define HD44780_DISPLAYON           0x04
#define HD44780_CURSORON            0x02
#define HD44780_BLINKON             0x01

/* Flags for display/cursor shift */
#define HD44780_DISPLAYMOVE         0x08
#define HD44780_CURSORMOVE          0x00
#define HD44780_MOVERIGHT           0x04
#define HD44780_MOVELEFT            0x00

/* Flags for function set */
#define HD44780_8BITMODE            0x10
#define HD44780_4BITMODE            0x00
#define HD44780_2LINE               0x08
#define HD44780_1LINE               0x00
#define HD44780_5x10DOTS            0x04
#define HD44780_5x8DOTS             0x00




//-----[ Alphanumeric LCD16X2 Functions ]-----

void LCD16X2_DATA(uint8_t LCD16X2_Index, unsigned char Data)
{
    if(Data & 1)
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D4_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D4_PINx, 1);
    else
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D4_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D4_PINx, 0);
    if(Data & 2)
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D5_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D5_PINx, 1);
    else
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D5_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D5_PINx, 0);
    if(Data & 4)
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D6_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D6_PINx, 1);
    else
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D6_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D6_PINx, 0);
    if(Data & 8)
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D7_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D7_PINx, 1);
    else
    	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].D7_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].D7_PINx, 0);
}

void LCD16X2_CMD(uint8_t LCD16X2_Index, unsigned char CMD)
{
    // Select Command Register
	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].RS_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].RS_PINx, 0);
    // Move The Command Data To LCD
	LCD16X2_DATA(LCD16X2_Index, CMD);
    // Send The EN Clock Signal
    HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 0);
    DELAY_US(25);
    HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 1);
    DELAY_US(25);
    HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 0);
    DELAY_US(25);
}

void LCD16X2_Clear(uint8_t LCD16X2_Index)
{
	HAL_Delay(100);
	LCD16X2_CMD(LCD16X2_Index, 0);
	LCD16X2_CMD(LCD16X2_Index, 1);
    DELAY_MS(20);
}

void LCD16X2_Set_Cursor(uint8_t LCD16X2_Index, unsigned char r, unsigned char c)
{
    unsigned char Temp,Low4,High4;
    if(r == 1)
    {
      Temp  = 0x80 + c - 1; //0x80 is used to move the cursor
      High4 = Temp >> 4;
      Low4  = Temp & 0x0F;
      LCD16X2_CMD(LCD16X2_Index, High4);
      LCD16X2_CMD(LCD16X2_Index, Low4);
    }
    if(r == 2)
    {
      Temp  = 0xC0 + c - 1;
      High4 = Temp >> 4;
      Low4  = Temp & 0x0F;
      LCD16X2_CMD(LCD16X2_Index, High4);
      LCD16X2_CMD(LCD16X2_Index, Low4);
    }
}

void LCD16X2_Init(uint8_t LCD16X2_Index)
{

	HAL_Delay(400);
	// According To Datasheet, We Must Wait At Least 40ms After Power Up Before Interacting With The LCD Module
	while(HAL_GetTick() < 20);
	// The Init. Procedure As Described In The Datasheet
	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].RS_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].RS_PINx, 0);
	HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 0);
    // Init in 4-Bit Data Mode
	LCD16X2_DATA(LCD16X2_Index, 0x00);
    DELAY_MS(500);
    LCD16X2_CMD(LCD16X2_Index, 0x03);
    DELAY_MS(500);
    LCD16X2_CMD(LCD16X2_Index, 0x03);
    DELAY_MS(500);
    LCD16X2_CMD(LCD16X2_Index, 0x03);
    DELAY_US(500);
    // The Rest of The Init Sequence As Defined in The Hitachi HD44780 Datasheet
    LCD16X2_CMD(LCD16X2_Index, 0x02);
    LCD16X2_CMD(LCD16X2_Index, 0x02);
    LCD16X2_CMD(LCD16X2_Index, 0x08);
    LCD16X2_CMD(LCD16X2_Index, 0x00);
    LCD16X2_CMD(LCD16X2_Index, 0x0C);
    LCD16X2_CMD(LCD16X2_Index, 0x00);
    LCD16X2_CMD(LCD16X2_Index, 0x06);
    LCD16X2_CMD(LCD16X2_Index, 0x00);
    LCD16X2_CMD(LCD16X2_Index, 0x01);
}

void LCD16X2_Write_Char(uint8_t LCD16X2_Index, char Data)
{
   char Low4,High4;
   Low4  = Data & 0x0F;
   High4 = Data & 0xF0;

   HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].RS_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].RS_PINx, 1);

   LCD16X2_DATA(LCD16X2_Index, (High4>>4));
   HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 0);
   DELAY_US(25);
   HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 1);
   DELAY_US(25);
   HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 0);
   DELAY_US(25);

   LCD16X2_DATA(LCD16X2_Index, Low4);
   HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 0);
   DELAY_US(25);
   HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 1);
   DELAY_US(25);
   HAL_GPIO_WritePin(LCD16X2_CfgParam[LCD16X2_Index].EN_GPIOx, LCD16X2_CfgParam[LCD16X2_Index].EN_PINx, 0);
   DELAY_US(25);
}

void LCD16X2_Write_String(uint8_t LCD16X2_Index, char *str)
{
    int i;
    for(i=0; str[i]!='\0'; i++)
    {
    	LCD16X2_Write_Char(LCD16X2_Index, str[i]);
    }
}

void LCD16X2_SL(uint8_t LCD16X2_Index)
{
	LCD16X2_CMD(LCD16X2_Index, 0x01);
	LCD16X2_CMD(LCD16X2_Index, 0x08);
}

void LCD16X2_SR(uint8_t LCD16X2_Index)
{
	LCD16X2_CMD(LCD16X2_Index, 0x01);
	LCD16X2_CMD(LCD16X2_Index, 0x0C);
}



void LCD16X2_CreateCustomChar(uint8_t LCD16X2_Index, uint8_t location, uint8_t *data)
{
    uint8_t i;
    location &= 0x07; // Only 0~7 allowed (3bit)

    // Set CGRAM address command (location << 3)
    LCD16X2_CMD(LCD16X2_Index, (0x40 | (location << 3)));

    for (i = 0; i < 8; i++) {
        LCD16X2_Write_Char(LCD16X2_Index, data[i]);
    }
}

void LCD16X2_WriteCustomChar(uint8_t LCD16X2_Index, uint8_t row, uint8_t col, uint8_t location)
{
    LCD16X2_Set_Cursor(LCD16X2_Index, row, col);
    LCD16X2_Write_Char(LCD16X2_Index, location);
}

void LCD16X2_DisplayOff(uint8_t LCD16X2_Index)
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0x08);
}


void LCD16X2_DisplayOn(uint8_t LCD16X2_Index) // C OFF B OFF
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0b1100);
}

void LCD16X2_CursorOn(uint8_t LCD16X2_Index) // C ON B OFF
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0b1110);
}

void LCD16X2_BlinkOn(uint8_t LCD16X2_Index) // C ON B ON
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0b1111);
}

void LCD16X2_SetCGAddr (uint8_t LCD16X2_Index)
{

}

void LCD16X2_EntryMode_IDSH11 (uint8_t LCD16X2_Index)
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0b0111);
}

void LCD16X2_EntryMode_IDSH01 (uint8_t LCD16X2_Index)
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0b0101);
}

void LCD16X2_EntryMode_IDSH10 (uint8_t LCD16X2_Index)
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0b0110);
}

void LCD16X2_EntryMode_IDSH00 (uint8_t LCD16X2_Index)
{
	LCD16X2_CMD(LCD16X2_Index, 0x00);
	LCD16X2_CMD(LCD16X2_Index, 0b0100);
}


///////////////////////////////////////////////////////


typedef enum{
	LCD_SCROLL_RTL,
	LCD_SCROLL_LTR
} LCDScroll;


void LCD16X2_ScrollTextDelay(uint8_t LCD16X2_Index,
                             const char* str,
                             uint16_t scroll_delay_ms,
                             uint16_t pause_ms,
                             uint8_t row,
                             uint8_t dir)
{
    const size_t N = 16;  // LCD 한 줄에 16 문자
    size_t len = strlen(str);
    size_t pad = N;
    size_t buf_len = pad + len + pad;

    char* buf = malloc(buf_len + 1);
    if (!buf) return;

    // [빈칸 x pad][문자열][빈칸 x pad]
    memset(buf, ' ', pad);
    memcpy(buf + pad, str, len);
    memset(buf + pad + len, ' ', pad);
    buf[buf_len] = '\0';

    // 좌→우
    if (dir == 0) {
        for (int pos = (int)(buf_len - N); pos >= 0; pos--) {
            LCD16X2_Set_Cursor(LCD16X2_Index, row, 1);
            char window[17] = {0};
            strncpy(window, buf + pos, N);
            LCD16X2_Write_String(LCD16X2_Index, window);

            if (pos == (int)pad) HAL_Delay(pause_ms);
            else HAL_Delay(scroll_delay_ms);
        }
    }
    // 우→좌
    else {
        for (size_t pos = 0; pos + N <= buf_len; pos++) {
            LCD16X2_Set_Cursor(LCD16X2_Index, row, 1);
            char window[17] = {0};
            strncpy(window, buf + pos, N);
            LCD16X2_Write_String(LCD16X2_Index, window);

            if (pos == pad) HAL_Delay(pause_ms);
            else HAL_Delay(scroll_delay_ms);
        }
    }

    free(buf);
}




/**
 * 16×2 한 줄(row)에 문자열을 스크롤해서 stop_col(1~16)에 '왼쪽 끝'이 오면 멈춘다.
 * - idx        : LCD 인덱스
 * - row        : 1 또는 2
 * - str        : 출력할 문자열 (ASCII)
 * - dir        : LCD_SCROLL_RTL / LCD_SCROLL_LTR
 * - stop_col   : 멈출 때 문자열의 '왼쪽 끝' 컬럼 (1~16)
 * - step_delay : 각 스텝 사이 지연(ms) — HAL_Delay 사용(블로킹)
 * LCD16X2_ScrollRTLTo(0, 2, "HELLO", 3, 40);  // 40ms 속도로 스르륵, 2행 3열에 정지
 */
void LCD16X2_ScrollToPos(uint8_t idx,
                         uint8_t row,
                         const char* str,
                         LCDScroll dir,
                         uint8_t stop_col,
                         uint16_t step_delay)
{
    const int N = 16;                         // 한 줄 폭
    if (row < 1 || row > 2) return;
    if (stop_col < 1) stop_col = 1;
    if (stop_col > N) stop_col = N;

    int len = 0;
    while (str[len] != '\0') { if (len < 255) len++; else break; } // 안전 길이 계산

    // pos = 문자열 "왼쪽 끝"의 화면 상 0-기반 x좌표 (0..15), 화면 밖도 허용
    // 멈출 목표 위치
    const int pos_stop = (int)stop_col - 1;

    int pos, pos_start, pos_end, step;

    if (dir == LCD_SCROLL_RTL) {
        // 처음엔 문자열이 화면 오른쪽 바깥에서 들어오게: 첫 프레임 '_____H'
        pos_start = N - 1;         // 오른쪽 가장자리에 첫 글자 'H'
        pos_end   = pos_stop;      // 목표 자리까지 왼쪽으로 이동
        step      = -1;            // 오른쪽→왼쪽
    } else {
        // 왼쪽에서 오른쪽으로: 화면 밖에서부터 들어오게
        // (왼쪽에서 들어오면 꼬리 글자가 먼저 보일 수 있지만, 규칙성은 유지)
        pos_start = -len + 1;      // 완전 바깥(왼쪽)에서 시작
        if (pos_start > pos_stop) pos_start = pos_stop; // 너무 짧은 문자열 보호
        pos_end   = pos_stop;
        step      = +1;
    }

    // 메인 루프: 각 프레임마다 16칸 "창"을 덮어 씀
    for (pos = pos_start; ; pos += step) {
        char window[17];
        for (int col = 0; col < N; ++col) {
            // 이 칸에 표시해야 할 문자의 str[] 인덱스
            int src = col - pos;   // 화면 col에 놓인 글자 = str[src]
            if (src >= 0 && src < len) window[col] = str[src];
            else                       window[col] = ' ';
        }
        window[N] = '\0';

        LCD16X2_Set_Cursor(idx, row, 1);
        LCD16X2_Write_String(idx, window);

        if (pos == pos_end) break;     // 정확히 원하는 자리에 왔으면 멈춤
        HAL_Delay(step_delay);
    }
}

// 편의 함수: 오른쪽→왼쪽으로 스크롤해 stop_col에서 멈춤
static inline void LCD16X2_ScrollRTLTo(uint8_t idx,
                                       uint8_t row,
                                       const char* str,
                                       uint8_t stop_col,
                                       uint16_t step_delay)
{
    LCD16X2_ScrollToPos(idx, row, str, LCD_SCROLL_RTL, stop_col, step_delay);
}





/// 커스텀 캐릭터
// ────────────────────────────────────────────────
// 1) 헬퍼: 4비트 모드 명령 전송
static void LCD16X2_SendCommandByte(uint8_t idx, uint8_t cmd) {
    uint8_t high = (cmd >> 4) & 0x0F;
    uint8_t low  = cmd & 0x0F;
    // 명령 모드
    HAL_GPIO_WritePin(LCD16X2_CfgParam[idx].RS_GPIOx,
                      LCD16X2_CfgParam[idx].RS_PINx, 0);
    // 상위 4비트
    LCD16X2_DATA(idx, high);
    // EN 펄스
    HAL_GPIO_WritePin(LCD16X2_CfgParam[idx].EN_GPIOx,
                      LCD16X2_CfgParam[idx].EN_PINx, 0);
    DELAY_US(25);
    HAL_GPIO_WritePin(LCD16X2_CfgParam[idx].EN_GPIOx,
                      LCD16X2_CfgParam[idx].EN_PINx, 1);
    DELAY_US(25);
    HAL_GPIO_WritePin(LCD16X2_CfgParam[idx].EN_GPIOx,
                      LCD16X2_CfgParam[idx].EN_PINx, 0);
    DELAY_US(25);
    // 하위 4비트
    LCD16X2_DATA(idx, low);
    HAL_GPIO_WritePin(LCD16X2_CfgParam[idx].EN_GPIOx,
                      LCD16X2_CfgParam[idx].EN_PINx, 0);
    DELAY_US(25);
    HAL_GPIO_WritePin(LCD16X2_CfgParam[idx].EN_GPIOx,
                      LCD16X2_CfgParam[idx].EN_PINx, 1);
    DELAY_US(25);
    HAL_GPIO_WritePin(LCD16X2_CfgParam[idx].EN_GPIOx,
                      LCD16X2_CfgParam[idx].EN_PINx, 0);
    DELAY_US(25);
}

// ────────────────────────────────────────────────
// 2) 커스텀 문자 등록 함수
//    idx      : LCD 인덱스
//    location : 0~7 CGRAM 슬롯
//    pattern  : 8바이트 패턴 배열
void LCD16X2_RegisterCustomChar(uint8_t idx,
                                uint8_t location,
                                uint8_t *pattern)
{
    location &= 0x07;  // 0~7 제한
    uint8_t addrCmd = HD44780_SETCGRAMADDR | (location << 3);
    // CGRAM 주소 설정
    LCD16X2_SendCommandByte(idx, addrCmd);
    // 패턴 8바이트 쓰기
    for (uint8_t i = 0; i < 8; i++) {
        LCD16X2_Write_Char(idx, pattern[i]);
    }
}

// ────────────────────────────────────────────────
// 3) 커스텀 문자 화면 출력 함수
//    idx, row, col, slot번호(location)
void LCD16X2_DisplayCustomChar(uint8_t idx,
                               uint8_t row,
                               uint8_t col,
                               uint8_t location)
{
    LCD16X2_Set_Cursor(idx, row, col);
    LCD16X2_Write_Char(idx, location);
}

// ────────────────────────────────────────────────
// 4) 커스텀 문자 삭제 함수
//    0번~7번 모두 빈칸 패턴으로 덮어쓰기
void LCD16X2_ClearCustomChars(uint8_t idx)
{
    uint8_t blank[8] = {0};
    for (uint8_t loc = 0; loc < 8; loc++) {
        LCD16X2_RegisterCustomChar(idx, loc, blank);
    }

}

//=============================================================
//================= LCD ANIMATION EFFECT ======================
//=============================================================
//#include "lcd16x2_anim.h" <- 사용시 이것을 추가할 것
#include "LCD16X2.h" // LCD16X2_Set_Cursor / LCD16X2_Write_String 사용  :contentReference[oaicite:1]{index=1}

#ifndef LCD16X2_COLS
#define LCD16X2_COLS 16
#endif

// ===== 내부 헬퍼 =====

// Q10 고정소수점(0..1024): easeInOutCubic
static inline int32_t ease_inout_cubic_q10(int step, int steps) {
    if (steps <= 0) return 1024;
    int32_t t = (int32_t)((step * 1024LL + steps/2) / steps); // 0..1024
    if (t < 512) {
        // 4*t^3 (t∈[0,0.5])  ≈ round(4*x^3 / 2^20)
        int64_t x = t;
        return (int32_t)((4LL * x * x * x + (1<<19)) >> 20);
    } else {
        // 1 - ((-2t+2)^3)/2
        int32_t u = 2048 - (t<<1);
        int64_t x = u;
        int32_t half = (int32_t)((x * x * x + (1<<20)) >> 21);
        return 1024 - half;
    }
}

// 윈도우(열 win_col..win_col+win_width-1)에 from/to를 겹쳐 그리기 (to 우선)
static inline void render_blend(uint8_t lcd_idx, uint8_t row,
                                uint8_t win_col, uint8_t win_width,
                                const char* from, int lf, int posOld,
                                const char* to,   int lt, int posNew) {
    if (win_width < 1) return;
    if (win_col < 1) win_col = 1;
    if (win_col + win_width - 1 > LCD16X2_COLS)
        win_width = LCD16X2_COLS - (win_col - 1);

    char win[17];
    if (win_width > 16) win_width = 16;
    for (int i=0;i<win_width;i++) win[i] = ' ';

    for (int i=0;i<lf;i++) {
        int col = posOld + i;
        if (0 <= col && col < win_width) win[col] = from[i];
    }
    for (int i=0;i<lt;i++) {
        int col = posNew + i;
        if (0 <= col && col < win_width) win[col] = to[i];
    }
    win[win_width] = '\0';

    LCD16X2_Set_Cursor(lcd_idx, row, win_col);
    LCD16X2_Write_String(lcd_idx, win);
}

// 중앙 정렬 좌표(윈도우 기준)
static inline int base_center(int win_w, int len) {
    if (len > win_w) len = win_w;
    return (win_w - len) / 2;
}

// ===== 공개 API =====

void LCDAnim_BeginSlide(LCDAnim* a,
                        uint8_t lcd_idx, uint8_t row,
                        uint8_t win_col, uint8_t win_width,
                        const char* from, const char* to,
                        LCDAnimDir dir,
                        uint16_t steps, uint16_t frame_delay_ms,
                        LCDAnimEase ease)
{
    if (!a) return;

    a->lcd_idx   = lcd_idx;
    a->row       = (row < 1 || row > 2) ? 1 : row;
    a->win_col   = (win_col < 1) ? 1 : win_col;
    if (win_width < 1) win_width = 1;
    if (a->win_col + win_width - 1 > LCD16X2_COLS)
        win_width = LCD16X2_COLS - (a->win_col - 1);
    a->win_width = win_width;

    // 문자열 복사(최대 32)
    a->from[0] = a->to[0] = '\0';
    if (from) strncpy(a->from, from, 32);
    if (to)   strncpy(a->to,   to,   32);
    a->from[32] = a->to[32] = '\0';

    a->len_from = 0; while (a->from[a->len_from] && a->len_from < 32) a->len_from++;
    a->len_to   = 0; while (a->to[a->len_to]     && a->len_to   < 32) a->len_to++;

    a->dir      = dir;
    a->ease     = ease;
    a->frame_delay_ms = (frame_delay_ms == 0) ? 25 : frame_delay_ms;
    a->steps    = (steps < 8) ? 8 : steps; // 자연스러움 확보

    // 시작/끝 좌표 설정
    int base_from_pos = base_center(a->win_width, a->len_from);
    int base_to_pos   = base_center(a->win_width, a->len_to);

    if (dir == LCDANIM_DIR_RTL) {
        // 이전: 중앙 → 왼쪽 바깥,  다음: 오른쪽 바깥 → 중앙
        a->startOld = base_from_pos;  a->endOld = -a->len_from;
        a->startNew = a->win_width;   a->endNew = base_to_pos;
    } else { // LTR
        // 이전: 중앙 → 오른쪽 바깥, 다음: 왼쪽 바깥 → 중앙
        a->startOld = base_from_pos;  a->endOld = a->win_width;
        a->startNew = -a->len_to;     a->endNew = base_to_pos;
    }

    a->t_start_ms = 0;  // Update에서 now_ms로 세팅
    a->running    = true;
}

bool LCDAnim_Update(LCDAnim* a, uint32_t now_ms)
{
    if (!a || !a->running) return false;

    if (a->t_start_ms == 0) a->t_start_ms = now_ms;
    uint32_t elapsed = now_ms - a->t_start_ms;

    // 현재 스텝
    uint32_t step = elapsed / a->frame_delay_ms;
    if (step > a->steps) step = a->steps;

    // 이징
    int32_t w_q10 = 1024;
    switch (a->ease) {
        case LCDANIM_EASE_INOUT_CUBIC:
        default:
            w_q10 = ease_inout_cubic_q10((int)step, a->steps);
            break;
    }

    // 보간 위치
    int posOld = a->startOld + (int)(( (int64_t)(a->endOld - a->startOld) * w_q10 + 512) >> 10);
    int posNew = a->startNew + (int)(( (int64_t)(a->endNew - a->startNew) * w_q10 + 512) >> 10);

    // 렌더
    render_blend(a->lcd_idx, a->row, a->win_col, a->win_width,
                 a->from, a->len_from, posOld,
                 a->to,   a->len_to,   posNew);

    if (step >= a->steps) {
        // 마지막 프레임(중앙 정렬 스냅)
        // 중앙에 최종 문자열 고정
        char line[17]; for (int i=0;i<16;i++) line[i]=' ';
        int left = base_center(a->win_width, a->len_to);
        int copy = a->len_to; if (copy > a->win_width) copy = a->win_width;
        for (int i=0;i<copy;i++) line[i] = a->to[i];
        line[ (a->win_width > 16 ? 16 : a->win_width) ] = '\0';

        LCD16X2_Set_Cursor(a->lcd_idx, a->row, a->win_col);
        LCD16X2_Write_String(a->lcd_idx, line);

        a->running = false;
        return false;
    }
    return true;
}

void LCDAnim_RunBlocking(LCDAnim* a)
{
    if (!a) return;
    uint32_t now = 0;

    // 간단 블로킹 루프
    while (a->running) {
        // now는 HAL_GetTick()이 일반적
        extern uint32_t HAL_GetTick(void);
        now = HAL_GetTick();
        bool cont = LCDAnim_Update(a, now);
        if (!cont) break;

        extern void HAL_Delay(uint32_t);
        HAL_Delay(a->frame_delay_ms);
    }
}



/*
 * 사용 예(논블로킹: 메인 루프/ 타이머)
 * // 전역(또는 static)
LCDAnim g_anim;

// 트리거 시(예: 인터럽트/버튼 등)
void OnModeChange(void) {
    // 2행, 전체 16칸 윈도우에서 "OLD" → "NEW"를 오른→왼으로 자연 슬라이드
    LCDAnim_BeginSlide(&g_anim,
        //lcd_idx 0, row 2,
		//win_col 1, /*win_width 16,
		//from "PRACTICE", /*to "TUNER",
		//dir LCDANIM_DIR_RTL,
		//steps 24,
		//frame_delay_ms 25,
        //LCDANIM_EASE_INOUT_CUBIC);
}


// 주기적으로 호출(메인루프 or SysTick)
void UI_Tick(void) {
    if (g_anim.running) {
        LCDAnim_Update(&g_anim, HAL_GetTick());
    }
}

 *
 *
 *
 */

