/*
 * File: LCD16X2.h
 * Driver Name: [[ LCD16x2 Display (GPIO 4-Bit Mode) ]]
 * SW Layer:   ECUAL
 * Created on: Jun 28, 2020
 * Ver: 1.1
 * Author:     Khaled Magdy
 * -------------------------------------------
 * For More Information, Tutorials, etc.
 * Visit Website: www.DeepBlueMbedded.com
 *
 */

#ifndef LCD16X2_H_
#define LCD16X2_H_

#include "stdint.h"

#define LCD16X2_MAX	1	// Maximum Number of LCD16x2 Modules in Your Project
#define LCD16X2_1	0	// LCD16X2 Instance Number 1 (Add more if you need)


//-----[ Prototypes For All Functions ]-----

extern void LCD16X2_Init(uint8_t);   // Initialize The LCD For 4-Bit Interface
extern void LCD16X2_Clear(uint8_t);  // Clear The LCD Display

extern void LCD16X2_Set_Cursor(uint8_t, unsigned char, unsigned char);  // Set Cursor Position

extern void LCD16X2_Write_Char(uint8_t, char);    // Write Character To LCD At Current Position
extern void LCD16X2_Write_String(uint8_t, char*); // Write A String To LCD

extern void LCD16X2_SL(uint8_t);  // Shift The Entire Display To The Left
extern void LCD16X2_SR(uint8_t);  // Shift The Entire Display To The Right



extern void LCD16X2_CreateCustomChar(uint8_t LCD16X2_Index, uint8_t location, uint8_t *data);
extern void LCD16X2_WriteCustomChar(uint8_t LCD16X2_Index, uint8_t row, uint8_t col, uint8_t location);

extern void LCD16X2_DisplayOn(uint8_t LCD16X2_Index);
extern void LCD16X2_DisplayOff(uint8_t LCD16X2_Index);

extern void LCD16X2_CursorOn(uint8_t LCD16X2_Index);
extern void LCD16X2_CursorOff(uint8_t LCD16X2_Index);

extern void LCD16X2_BlinkOn(uint8_t LCD16X2_Index);
extern void LCD16X2_BlinkOff(uint8_t LCD16X2_Index);

extern void LCD16X2_ScrollLeft(uint8_t LCD16X2_Index);
extern void LCD16X2_ScrollRight(uint8_t LCD16X2_Index);

extern void LCD16X2_EntryMode_IDSH11 (uint8_t LCD16X2_Index);
extern void LCD16X2_EntryMode_IDSH01 (uint8_t LCD16X2_Index);
extern void LCD16X2_EntryMode_IDSH10 (uint8_t LCD16X2_Index);
extern void LCD16X2_EntryMode_IDSH00 (uint8_t LCD16X2_Index);



//=============================================================
//================= LCD ANIMATION EFFECT ======================
//=============================================================
#ifndef LCD16X2_ANIM_H
#define LCD16X2_ANIM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

// ── 방향 ──
typedef enum {
    LCDANIM_DIR_LTR = 0,   // 왼쪽 밖 → 안쪽(오른쪽)으로 들어옴 (기존 텍스트는 오른쪽으로 빠짐)
    LCDANIM_DIR_RTL = 1    // 오른쪽 밖 → 안쪽(왼쪽)으로 들어옴 (기존 텍스트는 왼쪽으로 빠짐)
} LCDAnimDir;

// ── 이징 종류 (필요시 확장) ──
typedef enum {
    LCDANIM_EASE_INOUT_CUBIC = 0
} LCDAnimEase;

// ── 애니 인스턴스 ──
typedef struct {
    // 대상 LCD
    uint8_t lcd_idx;
    uint8_t row;              // 1 또는 2

    // 윈도우(한 줄에서 사용할 칸 영역)
    uint8_t win_col;          // 시작 열(1~16)
    uint8_t win_width;        // 폭(1~16, win_col+win_width-1 <= 16)

    // 문자열 버퍼(최대 32까지 수용; 필요시 증가)
    char from[33];
    char to[33];
    int  len_from;
    int  len_to;

    // 위치(윈도우 좌표 0~win_width-1 기준)
    int  startOld, endOld;
    int  startNew, endNew;

    // 타이밍
    uint16_t frame_delay_ms;  // 프레임 간 딜레이 (일정)
    uint16_t steps;           // 총 프레임수(>=8 권장)
    uint32_t t_start_ms;      // 논블로킹 시작 시각
    bool     running;         // 진행 중 여부

    // 모드
    LCDAnimDir  dir;
    LCDAnimEase ease;
} LCDAnim;

// ── API ──
// 논블로킹 시작(포인터 인자로 텍스트/영역/타이밍/이징 지정)
void LCDAnim_BeginSlide(LCDAnim* a,
                        uint8_t lcd_idx, uint8_t row,
                        uint8_t win_col, uint8_t win_width,
                        const char* from, const char* to,
                        LCDAnimDir dir,
                        uint16_t steps, uint16_t frame_delay_ms,
                        LCDAnimEase ease);

// 논블로킹 업데이트 (now_ms = HAL_GetTick() 등)
bool LCDAnim_Update(LCDAnim* a, uint32_t now_ms);

// 블로킹 실행(간이용)
void LCDAnim_RunBlocking(LCDAnim* a);

#endif // LCD16X2_ANIM_H


#endif /* LCD16X2_H_ */

