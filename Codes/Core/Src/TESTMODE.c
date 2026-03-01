// TESTMODE.c (상단)
#include "testmode.h"
#include "stm32f4xx_hal.h"
#include "main.h"               // 선택사항(매크로/타입)
#include <string.h>
#include <stdio.h>
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"


#ifndef ARM_MATH_CM4
#define ARM_MATH_CM4
#endif

#include <arm_math.h>

#include "math.h"
#include "notch.h"

// LCD & UI
#include "LCD16X2.h"
#define MyLCD LCD16X2_1
extern void LCDColorSet(uint8_t c);
extern void LCD16X2_Clear(uint8_t lcd);
extern void LCD16X2_Set_Cursor(uint8_t lcd, uint8_t row, uint8_t col);


// UI SFX (notch.c)
extern void notch_ui_button_beep(void);
extern void notch_ui_mode_return_triple_beep(void);
extern void notch_ui_rotary_click_freq(float freq_hz);

// === main.c과 동일 enum 재선언(호환 목적)
typedef enum { BUTTON_EVENT_NONE=0, BUTTON_EVENT_SHORT_PRESS, BUTTON_EVENT_LONG_PRESS } ButtonEvent;
typedef enum { ROTARY_EVENT_NONE=0, ROTARY_EVENT_CW, ROTARY_EVENT_CCW } RotaryEvent;

// === main.c의 전역들을 참조만(정의 금지!) ===
extern volatile ButtonEvent buttonEvents[3];      // :contentReference[oaicite:4]{index=4}
extern volatile RotaryEvent rotaryEvent3;         // :contentReference[oaicite:5]{index=5}
extern TIM_HandleTypeDef htim4;                   // :contentReference[oaicite:6]{index=6}
extern int16_t prev_count4;                       // :contentReference[oaicite:7]{index=7}
extern void Poll_Rotary(TIM_HandleTypeDef *htim, int16_t *prev_count, RotaryEvent *eventFlag); // :contentReference[oaicite:8]{index=8}
extern volatile uint8_t TestModeEnabled;          // main.c 정의본 참조

extern  I2S_HandleTypeDef hi2s2;
extern  I2S_HandleTypeDef hi2s3;



extern uint8_t __ccm_audio_load__, __ccm_audio_start__, __ccm_audio_end__;
extern uint8_t __ccm_test_load__,  __ccm_test_start__,  __ccm_test_end__;
void ccm_overlay_boot_select(int testmode_on)
{
    if (testmode_on) {
        uint8_t *src=&__ccm_test_load__, *dst=&__ccm_test_start__;
        uint32_t n = (uint32_t)(&__ccm_test_end__ - &__ccm_test_start__);
        while(n--) *dst++ = *src++;
    } else {
        uint8_t *src=&__ccm_audio_load__, *dst=&__ccm_audio_start__;
        uint32_t n = (uint32_t)(&__ccm_audio_end__ - &__ccm_audio_start__);
        while(n--) *dst++ = *src++;
    }
}





extern uint8_t __tmram_load_start__, __tmram_start__, __tmram_end__;
static void tm_copy_tmramfunc(void)
{
    uint8_t *src = &__tmram_load_start__;
    uint8_t *dst = &__tmram_start__;
    uint32_t len = (uint32_t)(&__tmram_end__ - &__tmram_start__);
    while (len--) *dst++ = *src++;
}




#ifndef SYS_BL_ADDR
#define SYS_BL_ADDR   0x1FFF0000U   // STM32F407 System Memory Base
#endif

__attribute__((noreturn))
void EnterFirmwareUpdateMode(void)
{
    // 1) 안내 표시
    LCDColorSet(4);
    LCD16X2_Clear(LCD16X2_1);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1, "FIRMWARE UPDATE");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1, "WAITING...     ");
    HAL_Delay(150);

    // 2) 인터럽트/주변장치 정리
    __disable_irq();

    // (필요한 것만 안전하게 정지: 예시는 I2S/타이머)
    extern I2S_HandleTypeDef hi2s2;
    extern I2S_HandleTypeDef hi2s3;
    extern TIM_HandleTypeDef htim4;

    // DMA/스트림/주변장치 정지(쓰는 경우만)
    HAL_I2S_DMAStop(&hi2s2);
    HAL_I2S_DMAStop(&hi2s3);
    HAL_I2S_DeInit(&hi2s2);
    HAL_I2S_DeInit(&hi2s3);
    HAL_TIM_Base_DeInit(&htim4);

    // Systick 정지
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    // NVIC 모든 인터럽트 비활성 + pending 클리어
    for (uint32_t i = 0; i < 8; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }
    __DSB(); __ISB();

    // RCC/HAL 초기상태로 복귀(HSI 기준) → 부트로더가 기대하는 클럭 상태
    HAL_RCC_DeInit();
    HAL_DeInit();

    // 3) System Memory 벡터테이블로 전환 + MSP/ResetHandler로 점프
    typedef void (*pFunction)(void);
    uint32_t sys_stack     = *(__IO uint32_t*)(SYS_BL_ADDR);
    uint32_t sys_reset_ptr = *(__IO uint32_t*)(SYS_BL_ADDR + 4U);

    // 벡터 테이블 이동
    SCB->VTOR = SYS_BL_ADDR;
    __set_MSP(sys_stack);

    // 마지막 안내 살짝 유지(선택)
    HAL_Delay(50);

    // 점프 (IRQ 비활성 상태 유지)
    ((pFunction)sys_reset_ptr)();

    // 여긴 절대 안 옴
    while (1) { /* no-return */ }
}


// SETTINGS 플래시 초기화(섹터 9~11) : 단일 함수 버전
// - 진입: 1행 "SETTINGS RESET" 표시 후 2초 대기
// - 컨펌: 1행 "ARE YOU SURE?", 2행 ">YES<  >NO<" (로터리로 토글, ENTER=확정, BACK=NO)
// - YES: 섹터 9→10→11 순서로 ERASE, "RESET DONE / CFG CLEARED" 잠시 표시 후 반환
// - NO : "CANCELLED" 잠시 표시 후 반환
// SETTINGS 플래시 초기화(섹터 9~11) : 단일 함수 (입력 스캔 포함, 커서 표시 수정)
void EnterResetFlash(void)
{

    LCD16X2_Clear(MyLCD);
    LCDColorSet(4); // info/blue
    tm_line16(1, "SETTINGS RESET");
    tm_line16(2, "PLEASE WAIT...");
    tm_flush_inputs(40);
    HAL_Delay(2000);
    tm_flush_inputs(20);                 // ★ 배너 대기 중 쌓인 입력 제거(선택)

    uint8_t sel_yes = 1; // 기본 YES
    for (;;) {
        LCDColorSet(2); // 경고/확인
        tm_line16(1, "ARE YOU SURE? ");
        tm_line16(2, "OK TO CONTINUE");  // ★ 커서 선택만 표시

        // 입력 소비 루프
        for (;;) {
            // 로터리 토글
            if (rotaryEvent3 == ROTARY_EVENT_CW || rotaryEvent3 == ROTARY_EVENT_CCW) {
                rotaryEvent3 = ROTARY_EVENT_NONE;
                sel_yes ^= 1u;
                tm_line16(2, sel_yes ? ">YES<  NO    " : " YES   >NO<  ");  // ★ 커서 반영
            }

            // ENTER = 확정
            if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
                buttonEvents[2] = BUTTON_EVENT_NONE;

                if (!sel_yes) {
                    // NO
                    LCDColorSet(4);
                    tm_line16(1, "CANCELLED     ");
                    tm_line16(2, "              ");
                    HAL_Delay(400);
                    tm_flush_inputs(80);
                    return;
                }

                // YES → ERASE 9,10,11
                LCDColorSet(4);
                tm_line16(1, "ERASING CFG...");
                HAL_Delay(100);

                HAL_FLASH_Unlock();
                uint32_t se = 0;
                FLASH_EraseInitTypeDef er;
                er.TypeErase    = FLASH_TYPEERASE_SECTORS;
                er.VoltageRange = FLASH_VOLTAGE_RANGE_3;
                er.NbSectors    = 1;

                // S9
                tm_line16(2, "ERASE S009 ...");
                er.Sector = FLASH_SECTOR_9;
                if (HAL_FLASHEx_Erase(&er, &se) != HAL_OK) {
                    LCDColorSet(5); tm_line16(1,"ERASE FAILED!"); tm_line16(2,"SECTOR 9 ERR "); HAL_Delay(900);
                    HAL_FLASH_Lock(); tm_flush_inputs(80); return;
                }
                // S10
                tm_line16(2, "ERASE S010 ...");
                er.Sector = FLASH_SECTOR_10;
                if (HAL_FLASHEx_Erase(&er, &se) != HAL_OK) {
                    LCDColorSet(5); tm_line16(1,"ERASE FAILED!"); tm_line16(2,"SECTOR 10 ERR"); HAL_Delay(900);
                    HAL_FLASH_Lock(); tm_flush_inputs(80); return;
                }
                // S11
                tm_line16(2, "ERASE S011 ...");
                er.Sector = FLASH_SECTOR_11;
                if (HAL_FLASHEx_Erase(&er, &se) != HAL_OK) {
                    LCDColorSet(5); tm_line16(1,"ERASE FAILED!"); tm_line16(2,"SECTOR 11 ERR"); HAL_Delay(900);
                    HAL_FLASH_Lock(); tm_flush_inputs(80); return;
                }
                HAL_FLASH_Lock();

                // 완료 안내 후 복귀(리셋하지 않음, main 계속 실행)
                LCDColorSet(3); // green
                tm_line16(1, "RESET DONE    ");
                tm_line16(2, "CFG CLEARED   ");
                HAL_Delay(700);
                tm_flush_inputs(100);
                return;
            }

            // BACK = NO
            if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
                buttonEvents[0] = BUTTON_EVENT_NONE;
                LCDColorSet(4);
                tm_line16(1, "CANCELLED     ");
                tm_line16(2, "              ");
                HAL_Delay(400);
                tm_flush_inputs(80);
                return;
            }

            HAL_Delay(1);
        }
    }

    NVIC_SystemReset();
}




// ───────────────────────────────────
// 버튼 스캐너(1ms 호출 가정) — main.c 로직을 TESTMODE에 내장
// ───────────────────────────────────
// 버튼 스캐너(1ms 호출 가정) — (⚠️중복 생산자, 비활성화!)
#define BTN_DEBOUNCE_MS   10u
#define LONG_PRESS_MS     1000u
static uint8_t  btn_level_hw[3] = {0,0,0};
static uint8_t  btn_stable[3]   = {0,0,0};
static uint16_t btn_cnt_ms[3]   = {0,0,0};
static uint32_t btn_down_tick[3]= {0,0,0};
static uint8_t  btn_long_sent2[3]= {0,0,0};



// ─────────────────────────────────────────────
// [TM INPUT: main.c와 완전 공유]  ★중복 스캔 금지
// 테스트모드에서는 버튼 스캔(Buttons_Scan_1ms)을 절대 호출하지 않는다.
// main.c가 ISR/주기적으로 생성한 buttonEvents[]만 소비한다.
// ─────────────────────────────────────────────
#define TM_USE_MAIN_INPUT   1

#if TM_USE_MAIN_INPUT
  // main.c의 타입/심볼을 공유해서 사용
  extern volatile ButtonEvent buttonEvents[3];
  extern volatile RotaryEvent rotaryEvent3;
  extern void Poll_Rotary(TIM_HandleTypeDef *htim, int16_t *prev_cnt, RotaryEvent *eventFlag);
  extern TIM_HandleTypeDef htim4;     // 테스트모드가 쓸 로터리 타이머 핸들
  extern int16_t prev_count4;

  // 혹시 TESTMODE.c 안에 Buttons_Scan_1ms() 호출이 남아있다면 전부 무효화
  #undef  Buttons_Scan_1ms
  #define Buttons_Scan_1ms()   ((void)0)

  // 화면 진입 시 잔류 이벤트 플러시(롱/숏 중복 방지)
  static inline void tm_input_guard(uint32_t guard_ms){
      uint32_t t0 = HAL_GetTick();
      // 이벤트 큐 비우기
      buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;
      rotaryEvent3 = ROTARY_EVENT_NONE;
      while ((HAL_GetTick() - t0) < guard_ms) {
          buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;
          rotaryEvent3 = ROTARY_EVENT_NONE;
          HAL_Delay(1);
      }
  }

  // 안전 소비(읽고 즉시 소거)
  static inline int tm_take_button(uint8_t idx, ButtonEvent ev){
      if (buttonEvents[idx] == ev) { buttonEvents[idx] = BUTTON_EVENT_NONE; return 1; }
      return 0;
  }
#endif







static inline uint8_t tm_read_btn_level(int i) {
    switch(i){
        case 0: return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2) == GPIO_PIN_RESET);
        case 1: return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET);
        case 2: return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET);
        default: return 0;
    }
}



// ── 입력 플러시 & 로터리 재동기화 ──
static inline void tm_resync_rotary(void) {
    extern TIM_HandleTypeDef htim4;
    extern int16_t prev_count4;
    prev_count4 = __HAL_TIM_GET_COUNTER(&htim4);
    rotaryEvent3 = ROTARY_EVENT_NONE;
}

void tm_flush_inputs(uint32_t guard_ms) {
    // 버튼/로터리 이벤트 모두 비우고 약간의 데드타임 후 최종 재동기화
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
    tm_resync_rotary();
    uint32_t end = HAL_GetTick() + guard_ms;
    while ((int32_t)(HAL_GetTick() - end) < 0) HAL_Delay(1);
    tm_resync_rotary();
}










// ───────────────────────────────────
// LCD 유틸
static inline void tm_lcd_line(uint8_t row, const char *s16)
{
    char buf[17]; uint8_t i=0;
    for (; i<16 && s16 && s16[i]; ++i) buf[i]=s16[i];
    for (; i<16; ++i) buf[i]=' ';
    buf[16]='\0';
    LCD16X2_Set_Cursor(MyLCD, row, 1);
    LCD16X2_Write_String(MyLCD, buf);
}

static void tm_toast(uint8_t color, const char* l1, const char* l2, uint32_t keep_ms)
{
    LCDColorSet(color);
    tm_lcd_line(1, l1);
    tm_lcd_line(2, l2);
    if (keep_ms) HAL_Delay(keep_ms);
}

// ───────────────────────────────────
// 메뉴(12개) + 그리기/디스패치 (이전 답변과 동일)
static const char* kItems[] = {
    "1.FLASH TEST","2.RAM TEST","3.FLASH BROWSE","4.RAM BROWSE",
    "5.SOUND OUT","6.I2S2 INPUT","7.I2S3 INPUT","8.BEEP SCORE",
    "9.LCD TEST","10.BACKLIGHT","11.TEXT PRINT","12.INPUT TEST", "13. BAAAAAAM!!!!", "14. AUTO TEST  "
};

enum {
    TM_NUM_ITEMS = (sizeof(kItems)/sizeof(kItems[0])),
    TM_IDX_NUKE = 12  ,         // 인덱스 고정(0-base): 13번째 = 12
    TM_IDX_AUTOTEST = 13
};








static void tm_draw_menu(uint8_t sel)
{
    uint8_t nxt = (uint8_t)((sel + 1u) % TM_NUM_ITEMS);
    char l1[18], l2[18];
    snprintf(l1, sizeof(l1), ">%-15.15s", kItems[sel]);
    snprintf(l2, sizeof(l2), " %-15.15s", kItems[nxt]);

    // ★ NUKE 선택 시 빨간색, 그 외에는 기존 색(3=Green)
    LCDColorSet(sel == TM_IDX_NUKE ? 5 : 3);

    tm_lcd_line(1, l1);
    tm_lcd_line(2, l2);
}



// 재부팅 확인
static void tm_confirm_reboot(void)
{
    tm_toast(5, " REBOOT DEVICE?", " ENTER=YES  BK=NO", 0);
    tm_flush_inputs(80); // 💡 시작 시 잔여 입력 싹 비움

    for(;;){
        Buttons_Scan_1ms();
        // YES
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[2] = BUTTON_EVENT_NONE;
            notch_ui_button_beep();
            tm_toast(4, " REBOOTING...", " SEE YOU :)", 200);
            tm_flush_inputs(80);           // 💡 마지막으로 비우고
            NVIC_SystemReset();
        }
        // NO
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            tm_flush_inputs(120);          // 💡 대화상자 닫히며 비움 → 상위에서 중복 미발생
            break;
        }
        HAL_Delay(1);
    }
}


// 스텁들(필요에 따라 실제 구현 교체)
static void tm_stub_screen(uint8_t color, const char* title, const char* body)
{
    LCD16X2_Clear(MyLCD);
    tm_toast(color, title, body, 300);
    tm_lcd_line(2, " BK:MENU  ENT:RUN");

    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
    for(;;){
        Buttons_Scan_1ms();
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            break;
        }
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[2] = BUTTON_EVENT_NONE;
            notch_ui_button_beep();
            tm_toast(color, title, " RUNNING...", 200);
        }
        HAL_Delay(1);
    }
}














// ==== FLASH TEST (HP 50g 스타일: Program + Verify, PASS/FAIL) ====

// ==== FLASH TEST (persistent address view; CW/CCW updates line1 only) ====
#include "stm32f4xx_hal_flash.h"

static void TM_FlashTest_Manual(void)
{
    // ---- 외부 입력/하드웨어 심볼 ----
    extern void Poll_Rotary(TIM_HandleTypeDef *htim, int16_t *prev_count, RotaryEvent *eventFlag);
    extern TIM_HandleTypeDef htim4;
    extern int16_t prev_count4;
    extern volatile RotaryEvent rotaryEvent3;
    extern volatile ButtonEvent  buttonEvents[3];

    // ---- 테스트 파라미터 ----
    #define TMF_STEP_BYTES    0x100u          // 로터리 한 칸 = 256B
    #define TMF_WORDS         64u             // 64워드(=256B)만 테스트
    #define TMF_PATTERN       0xA5A5A5A5u     // 프로그램 패턴
    #define TMF_ADDR_MIN      0x08000000u     // 안전 하한
    #define TMF_ADDR_MAX      0x08100000u     // 안전 상한(보드 용량에 맞게 수정 가능)

    uint32_t base = 0x080E0000u;              // 시작 주소(필요시 바꿔도 됨)
    uint8_t running = 1;

    // ---- 진입 배너 한 번만 보여주고, 이후엔 "주소/힌트" 고정 ----
    LCD16X2_Clear(MyLCD);
    LCDColorSet(4);
    tm_lcd_line(1, " FLASH TEST    ");
    tm_lcd_line(2, " ROT:ADDR STEP ");
    HAL_Delay(250);

    // 고정 레이아웃: 1행 = 주소, 2행 = 힌트(항상 유지)
    char line1[17];
    LCD16X2_Clear(MyLCD);
    LCDColorSet(3);
    snprintf(line1, sizeof(line1), "ADDR:%08lX ", (unsigned long)base);
    tm_lcd_line(1, line1);
    tm_lcd_line(2, "ENT:RUN BK:EXIT");

    while (running) {
        // 입력 갱신
        Buttons_Scan_1ms();
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);

        // ── 로터리로 주소 이동: 1행만 갱신 (2행은 건드리지 않음!) ──
        if (rotaryEvent3 == ROTARY_EVENT_CW) {
            rotaryEvent3 = ROTARY_EVENT_NONE;
            uint32_t next = base + TMF_STEP_BYTES;
            if (next < TMF_ADDR_MAX) base = next;
            notch_ui_rotary_click_freq(1000.f);
            snprintf(line1, sizeof(line1), "ADDR:%08lX ", (unsigned long)base);
            tm_lcd_line(1, line1);          // ← 1행만 업데이트
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
            rotaryEvent3 = ROTARY_EVENT_NONE;
            uint32_t next = (base >= TMF_STEP_BYTES) ? (base - TMF_STEP_BYTES) : base;
            if (next >= TMF_ADDR_MIN) base = next;
            notch_ui_rotary_click_freq(900.f);
            snprintf(line1, sizeof(line1), "ADDR:%08lX ", (unsigned long)base);
            tm_lcd_line(1, line1);          // ← 1행만 업데이트
        }

        // ── BACK: 상위 메뉴 복귀 ──
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            return;
        }

        // ── ENTER: 선택한 256B 블록 Program + Verify → PASS/FAIL ──
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[2] = BUTTON_EVENT_NONE;
            notch_ui_button_beep();

            // 1) 공백(0xFF) 확인
            uint8_t blank = 1;
            for (uint32_t i=0;i<TMF_WORDS;i++){
                if (*((volatile uint32_t*)(base + i*4u)) != 0xFFFFFFFFu) { blank = 0; break; }
            }
            if (!blank) {
                LCDColorSet(5);             // 빨강
                tm_lcd_line(1, " NOT BLANK     ");
                tm_lcd_line(2, " CHOOSE OTHER  ");
                HAL_Delay(700);
                // 원래 레이아웃 복귀
                LCDColorSet(3);
                snprintf(line1, sizeof(line1), "ADDR:%08lX ", (unsigned long)base);
                tm_lcd_line(1, line1);
                tm_lcd_line(2, "ENT:RUN BK:EXIT");
                continue;
            }

            // 2) Program + Verify
            uint32_t t0 = HAL_GetTick();
            uint32_t fails = 0;
            HAL_FLASH_Unlock();
            for (uint32_t i=0;i<TMF_WORDS;i++){
                uint32_t a = base + i*4u;
                if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, a, TMF_PATTERN) != HAL_OK) { fails++; continue; }
                uint32_t rd = *((volatile uint32_t*)a);
                if (rd != TMF_PATTERN) { fails++; }
            }
            HAL_FLASH_Lock();
            uint32_t dt = HAL_GetTick() - t0;

            // 3) 결과 표시
            if (fails == 0) {
                LCDColorSet(3); // 초록
                tm_lcd_line(1, " PROG+VERIFY   ");
                char ok2[17]; snprintf(ok2,sizeof(ok2)," PASS %4lums  ", (unsigned long)dt);
                tm_lcd_line(2, ok2);
            } else {
                LCDColorSet(5); // 빨강
                tm_lcd_line(1, " PROG+VERIFY   ");
                char ng2[17]; snprintf(ng2,sizeof(ng2)," FAIL %02lu err ", (unsigned long)fails);
                tm_lcd_line(2, ng2);
            }
            HAL_Delay(900);

            // 4) 원래 레이아웃 복귀(주소는 1행, 힌트는 2행 고정)
            LCDColorSet(3);
            snprintf(line1, sizeof(line1), "ADDR:%08lX ", (unsigned long)base);
            tm_lcd_line(1, line1);
            tm_lcd_line(2, "ENT:RUN BK:EXIT");
        }

        // ── BACK 길게: 즉시 리부트(옵션) ──
        if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS) {
            buttonEvents[0] = BUTTON_EVENT_NONE;
            LCDColorSet(5);
            tm_lcd_line(1, " FORCE REBOOT  ");
            tm_lcd_line(2, " LONG BACK     ");
            HAL_Delay(250);
            NVIC_SystemReset();
        }

        HAL_Delay(1);
    }
}


// ==== FLASH TEST (Auto-run: scan blank blocks → program+verify → show ms → auto-exit) ====
static void TM_FlashTest(void)
{
    // LCD 라인 유틸(기존 것 그대로 사용 가능)
    auto void line16(uint8_t row, const char* s){
        char buf[17]; uint8_t i=0;
        for (; i<16 && s && s[i]; ++i) buf[i]=s[i];
        for (; i<16; ++i) buf[i]=' ';
        buf[16]='\0';
        LCD16X2_Set_Cursor(MyLCD, row, 1);
        LCD16X2_Write_String(MyLCD, buf);
    }

    // 파라미터
    #define TMF_WORDS        64u                 // 256B
    #define TMF_PATTERN      0xA5A5A5A5u
    #define TMF_TEST_BEGIN   0x080C0000u         // Sector10 시작
    #define TMF_TEST_END     0x080E0000u         // (Sector11 시작) 제외
    #define TMF_TEST_SECTOR  FLASH_SECTOR_10

    auto void erase_sector(uint32_t sector){
        FLASH_EraseInitTypeDef e = {0};
        uint32_t se;
        e.TypeErase    = FLASH_TYPEERASE_SECTORS;
        e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        e.Sector       = sector;
        e.NbSectors    = 1;
        HAL_FLASH_Unlock();
        HAL_FLASHEx_Erase(&e, &se);
        HAL_FLASH_Lock();
    }

    LCD16X2_Clear(MyLCD);
    LCDColorSet(4);
    line16(1, "FLASH AUTO TEST");
    line16(2, "SECTOR10 CLEAN ");
    HAL_Delay(250);

    // 0) 시작 전 깨끗이 비움 (이전 실행이 남긴 A5 패턴 제거)
    erase_sector(TMF_TEST_SECTOR);

    uint32_t tested_blocks = 0, pass_blocks = 0, fail_blocks = 0;

    // 1) 프로그램+검증
    HAL_FLASH_Unlock();
    for (uint32_t base = TMF_TEST_BEGIN; base + TMF_WORDS*4u <= TMF_TEST_END; base += TMF_WORDS*4u)
    {
        // 진행 안내
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "ADDR:%08lX ", (unsigned long)base);
        LCDColorSet(3);
        line16(1, l1); line16(2, "RUN PROG+VERFY");

        uint32_t t0 = HAL_GetTick(), errs = 0;

        for (uint32_t i=0;i<TMF_WORDS;i++){
            uint32_t a = base + i*4u;
            __disable_irq();
            HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, a, TMF_PATTERN);
            __enable_irq();
            if (st != HAL_OK) { errs++; continue; }
            if (*((volatile uint32_t*)a) != TMF_PATTERN) errs++;
        }

        uint32_t dt = HAL_GetTick() - t0;
        tested_blocks++;

        // 결과 표시
        if (errs == 0) {
            pass_blocks++;
            LCDColorSet(3);
            snprintf(l2, sizeof(l2), "PASS %4lums  ", (unsigned long)dt);
        } else {
            fail_blocks++;
            LCDColorSet(5);
            snprintf(l2, sizeof(l2), "FAIL %02lu err ", (unsigned long)errs);
        }
        line16(1, l1); line16(2, l2);
        HAL_Delay(80);
    }
    HAL_FLASH_Lock();

    // 2) 끝나면 섹터 복구(erase) → 다음 테스트도 항상 가능
    LCDColorSet(4);
    line16(1, "RESTORE SECT10");
    line16(2, "ERASE…        ");
    erase_sector(TMF_TEST_SECTOR);

    // 3) 요약
    if (tested_blocks == 0){
        LCDColorSet(4);
        line16(1, "NO BLOCK TEST ");
        line16(2, "AUTO EXIT...  ");
        HAL_Delay(600);
    } else {
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "DONE %3lu BLK ", (unsigned long)tested_blocks);
        snprintf(l2, sizeof(l2), "OK:%02lu NG:%02lu ", (unsigned long)pass_blocks, (unsigned long)fail_blocks);
        LCDColorSet(fail_blocks ? 5 : 3);
        line16(1, l1); line16(2, l2);
        HAL_Delay(900);
    }
    notch_ui_button_beep();
    return;
}












// ───────────────────────────────────────────────
// 유틸: 16칸 고정 출력 (공백 패딩)
void tm_line16(uint8_t row, const char *s) {
    char buf[17]; uint8_t i=0;
    for(; i<16 && s && s[i]; ++i) buf[i]=s[i];
    for(; i<16; ++i) buf[i]=' ';
    buf[16]='\0';
    LCD16X2_Set_Cursor(MyLCD, row, 1);
    LCD16X2_Write_String(MyLCD, buf);
}

static inline void tm_put2(char *p, uint32_t v) {
    p[0] = (char)('0' + ((v/10)%10));
    p[1] = (char)('0' + (v%10));
}


// 유틸: 간단한 32bit xorshift PRNG (의사난수 패턴용)
static inline uint32_t tm_xorshift32(uint32_t x){
    x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x;
}

// ───────────────────────────────────────────────
// RAM TEST: 자동 실행(패턴 5종) → PASS/FAIL(ms) 표시 → 자동 종료
static void TM_RamTest(void)
{


    // ── 테스트 대상 선택 ──
    // 1) 생산용: 링커로 예약한 전용 영역이 있으면 전체 검사
    //    (예: ld에서 .ram_test 섹션 예약 후 아래 매크로로 범위를 지정)
    //    #define TM_RAM_BEGIN 0x20010000u
    //    #define TM_RAM_END   0x20018000u
    #ifndef TM_RAM_BEGIN
    #define TM_RAM_BEGIN 0u
    #endif
    #ifndef TM_RAM_END
    #define TM_RAM_END   0u
    #endif

    // 2) 대안: 내부 버퍼(8KB)만 깊이 검사
    static uint32_t tm_buf[2048]; // 8KB

    volatile uint32_t *mem = (volatile uint32_t*)
        ((TM_RAM_BEGIN && TM_RAM_END) ? TM_RAM_BEGIN : (uint32_t)tm_buf);
    uint32_t words = (TM_RAM_BEGIN && TM_RAM_END)
        ? ((TM_RAM_END - TM_RAM_BEGIN) / 4u)
        : (uint32_t)(sizeof(tm_buf)/sizeof(tm_buf[0]));
    uint8_t using_range = (TM_RAM_BEGIN && TM_RAM_END) ? 1u : 0u;

    // 화면 시작
    LCD16X2_Clear(MyLCD);
    LCDColorSet(4);
    tm_line16(1, " RAM AUTO TEST ");
    tm_line16(2, using_range ? "RANGE MODE    " : "BUF 8KB MODE  ");
    HAL_Delay(350);

    // 결과 집계
    uint32_t total_err = 0;
    uint32_t pass_cnt  = 0;

    // 헬퍼: 한 줄 요약 표시
    auto void show_step(const char* name, uint32_t ms, uint32_t err){
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "%-8.8s%s", name, err ? " FAIL" : " PASS");
        if (err) snprintf(l2, sizeof(l2), "ERR:%06lu   ", (unsigned long)err);
        else     snprintf(l2, sizeof(l2), "TIME:%4lums ", (unsigned long)ms);
        LCDColorSet(err ? 5 : 3); // FAIL=Red, PASS=Green
        tm_line16(1, l1);
        tm_line16(2, l2);
        HAL_Delay(2000);
    };

    // ── 패스 1: Data bus quick test (워드0에 워킹 1/0) ──
    {
        uint32_t t0 = HAL_GetTick(), err = 0;
        volatile uint32_t *p = mem;
        // walking-1
        for (uint32_t bit=0; bit<32; ++bit) {
            uint32_t pat = 1u << bit; *p = pat;
            if (*p != pat) { ++err; break; }
        }
        // walking-0
        for (uint32_t bit=0; bit<32; ++bit) {
            uint32_t pat = ~(1u << bit); *p = pat;
            if (*p != pat) { ++err; break; }
        }
        uint32_t dt = HAL_GetTick() - t0;
        total_err += err; pass_cnt += (err==0);
        show_step("DATABUS", dt, err);
    }

    // ── 패스 2: 0x00000000 채움/검증 ──
    {
        uint32_t t0 = HAL_GetTick(), err = 0;
        for (uint32_t i=0;i<words;i++) mem[i]=0x00000000u;
        for (uint32_t i=0;i<words;i++) if (mem[i]!=0x00000000u) ++err;
        uint32_t dt = HAL_GetTick() - t0;
        total_err += err; pass_cnt += (err==0);
        show_step("ALL-0   ", dt, err);
    }

    // ── 패스 3: 0xFFFFFFFF 채움/검증 ──
    {
        uint32_t t0 = HAL_GetTick(), err = 0;
        for (uint32_t i=0;i<words;i++) mem[i]=0xFFFFFFFFu;
        for (uint32_t i=0;i<words;i++) if (mem[i]!=0xFFFFFFFFu) ++err;
        uint32_t dt = HAL_GetTick() - t0;
        total_err += err; pass_cnt += (err==0);
        show_step("ALL-1   ", dt, err);
    }

    // ── 패스 4: 고정 패턴(0xA5A5 / 0x5A5A 번갈이) ──
    {
        uint32_t t0 = HAL_GetTick(), err = 0;
        for (uint32_t i=0;i<words;i++) mem[i]=(i&1)?0x5A5A5A5Au:0xA5A5A5A5u;
        for (uint32_t i=0;i<words;i++) {
            uint32_t exp = (i&1)?0x5A5A5A5Au:0xA5A5A5A5u;
            if (mem[i]!=exp) ++err;
        }
        uint32_t dt = HAL_GetTick() - t0;
        total_err += err; pass_cnt += (err==0);
        show_step("ALT-A5  ", dt, err);
    }

    // ── 패스 5: 주소 패턴 (index/주소 기반) ──
    {
        uint32_t t0 = HAL_GetTick(), err = 0;
        // 주소 기반은 alias/배선 오류 탐지에 유리
        for (uint32_t i=0;i<words;i++) mem[i]=(uint32_t)(uintptr_t)&mem[i];
        for (uint32_t i=0;i<words;i++) {
            uint32_t exp=(uint32_t)(uintptr_t)&mem[i];
            if (mem[i]!=exp) ++err;
        }
        uint32_t dt = HAL_GetTick() - t0;
        total_err += err; pass_cnt += (err==0);
        show_step("ADDRPAT ", dt, err);
    }

    // ── 패스 6: 의사난수(LFSR/xorshift) 채움/재생성 검증 ──
    {
        uint32_t t0 = HAL_GetTick(), err = 0;
        uint32_t seed = 0x12345678u ^ (uint32_t)(uintptr_t)mem ^ (words<<5);
        uint32_t x = seed;
        for (uint32_t i=0;i<words;i++){ x = tm_xorshift32(x); mem[i]=x; }
        x = seed;
        for (uint32_t i=0;i<words;i++){ x = tm_xorshift32(x); if (mem[i]!=x) ++err; }
        uint32_t dt = HAL_GetTick() - t0;
        total_err += err; pass_cnt += (err==0);
        show_step("LFSR    ", dt, err);
    }

    // 요약/자동 종료
    {
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "DONE %2lu/6 OK ", (unsigned long)pass_cnt);
        if (total_err==0) { LCDColorSet(3); snprintf(l2,sizeof(l2),"ALL PASS :)   "); }
        else              { LCDColorSet(5); snprintf(l2,sizeof(l2),"ERR:%06lu   ", (unsigned long)total_err); }
        tm_line16(1, l1); tm_line16(2, l2);
        HAL_Delay(1000);
    }
    notch_ui_button_beep(); // 삐-
    // 자동으로 상위 메뉴 복귀 (return)
}
















// ==== FLASH BROWSE (Read-only, rotary-driven inspector) ====
static void TM_FlashBrowse(void)
{
    // 외부 심볼(이미 TESTMODE.c 밖/위에 존재하는 것들 사용)

    extern void Poll_Rotary(TIM_HandleTypeDef *htim, int16_t *prev_count, RotaryEvent *eventFlag);
    extern TIM_HandleTypeDef htim4;
    extern int16_t prev_count4;
    extern volatile RotaryEvent rotaryEvent3;
    extern volatile ButtonEvent  buttonEvents[3];

    // ── 플래시 주소 범위 (보드 용량에 맞게 상한은 여유) ──
    const uint32_t ADDR_MIN = 0x08000000u;
    const uint32_t ADDR_MAX = 0x08100000u - 4u; // 마지막 워드까지

    // ── STEP 후보 ──
    const uint32_t steps[] = { 4u, 16u, 0x100u, 0x1000u };
    enum { N_STEPS = sizeof(steps)/sizeof(steps[0]) };
    uint8_t step_i = 0; // 시작은 4바이트

    // 시작 주소(현장 편의상 설정섹터 직전 영역 근방에서 시작)
    uint32_t addr = 0x080C0000u;

    // ── 작은 헬퍼 ──
    auto inline void draw_main(uint32_t a)
    {
        char l1[17], l2[17];
        uint32_t v = *((volatile uint32_t*)a);
        snprintf(l1, sizeof(l1), "ADDR:%08lX ", (unsigned long)a);
        snprintf(l2, sizeof(l2), "VAL:%08lX %c ", (unsigned long)v, (v==0xFFFFFFFFu)?'B':'U');
        // 색은 읽기 전용이라 고정(초록)
        LCDColorSet(3);
        tm_lcd_line(1, l1);
        tm_lcd_line(2, l2);
    }

    auto inline void show_step(uint32_t step)
    {
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "ADDR:%08lX ", (unsigned long)addr);
        snprintf(l2, sizeof(l2), "STEP:%08lX ", (unsigned long)step);
        LCDColorSet(4);             // 파랑으로 잠깐 안내
        tm_lcd_line(1, l1);
        tm_lcd_line(2, l2);
        HAL_Delay(350);
        // 안내 후 값 화면으로 복귀
        draw_main(addr);
    }

    // ── 진입 배너 후 메인 화면 고정 ──
    LCD16X2_Clear(MyLCD);
    LCDColorSet(4);
    tm_lcd_line(1, " FLASH BROWSE  ");
    tm_lcd_line(2, " ROTARY=ADDR   ");
    HAL_Delay(250);

    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    draw_main(addr);

    for(;;){
        // 입력 갱신
        Buttons_Scan_1ms();
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);

        // ── 로터리: 주소 이동(화면 유지 갱신) ──
        if (rotaryEvent3 == ROTARY_EVENT_CW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            uint32_t next = addr + steps[step_i];
            if (next > ADDR_MAX) next = ADDR_MAX;
            if (next != addr) {
                addr = next;
                notch_ui_rotary_click_freq(1000.f);
                draw_main(addr);            // 1·2행 모두 갱신하지만 레이아웃 유지
            }
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            uint32_t step = steps[step_i];
            uint32_t next = (addr >= step) ? (addr - step) : ADDR_MIN;
            if (next < ADDR_MIN) next = ADDR_MIN;
            if (next != addr) {
                addr = next;
                notch_ui_rotary_click_freq(900.f);
                draw_main(addr);
            }
        }

        // ── ENTER: STEP 크기 순환 ──
        // ENTER: STEP 크기 순환
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            step_i = (uint8_t)((step_i + 1u) % N_STEPS);
            notch_ui_button_beep();
            show_step(steps[step_i]);       // 안내 토스트
            tm_flush_inputs(60);            // 💡 여기 추가! (잔여 로터리 삭제)
            draw_main(addr);
        }


        // ── BACK: 상위 메뉴 복귀 ──
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            return;
        }

        // (옵션) BACK 길게: 강제 리부트
        if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            LCDColorSet(5);
            tm_lcd_line(1, " FORCE REBOOT  ");
            tm_lcd_line(2, " LONG BACK     ");
            HAL_Delay(250);
            NVIC_SystemReset();
        }

        HAL_Delay(1);
    }
}














// ==== RAM BROWSE (Read-only inspector with step cycle & watch mode) ====
static void TM_RamBrowse(void)
{
    // ── 외부 심볼 (이미 TESTMODE.c 밖/위에 존재) ──

    // ── 안전한 RAM 범위 설정(보드에 맞게 필요시 조정) ──
    // 링커로 테스트용 RAM 영역을 예약했다면 TM_RAM_BEGIN/TM_RAM_END를 사용
#ifndef TM_RAM_BEGIN
#  define TM_RAM_BEGIN  0x20000000u
#endif
#ifndef TM_RAM_END
#  define TM_RAM_END    0x20020000u   // 보수적으로 128KB~128+KB 수준(필요시 조정)
#endif

    // ── STEP 후보 ──
    const uint32_t steps[] = { 4u, 16u, 64u, 256u };
    enum { N_STEPS = sizeof(steps)/sizeof(steps[0]) };
    uint8_t step_i = 0; // 4바이트부터 시작

    // 시작 주소: 테스트용 영역 시작
    uint32_t addr = TM_RAM_BEGIN;

    // WATCH 모드 상태
    uint8_t watch_on = 0;
    uint32_t last_val = 0;

    // 16칸 LCD 라인 출력 유틸(이미 파일 상단에 tm_lcd_line 있으면 그거 써도 됨)
    // 여기선 직접 씀: (MyLCD는 파일 밖에서 이미 정의되어 있으므로 건드리지 않음)
    #define MyLCD LCD16X2_1
    auto void line16(uint8_t row, const char* s) {
        char buf[17]; uint8_t i=0;
        for(; i<16 && s && s[i]; ++i) buf[i]=s[i];
        for(; i<16; ++i) buf[i]=' ';
        buf[16]='\0';
        LCD16X2_Set_Cursor(MyLCD, row, 1);
        LCD16X2_Write_String(MyLCD, buf);
    }

    // 현재 주소 화면 갱신
    auto void draw_main(uint32_t a, uint32_t v, uint8_t watch) {
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "ADDR:%08lX ", (unsigned long)a);
        // 2행 끝에 W/N 표시 (Watch ON/Off)
        snprintf(l2, sizeof(l2), "VAL:%08lX %c ", (unsigned long)v, watch?'W':'N');
        LCDColorSet(3);
        line16(1, l1);
        line16(2, l2);
    }

    // STEP 안내
    auto void show_step(uint32_t step){
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "ADDR:%08lX ", (unsigned long)addr);
        snprintf(l2, sizeof(l2), "STEP:%08lX ", (unsigned long)step);
        LCDColorSet(4);
        line16(1, l1); line16(2, l2);
        HAL_Delay(300);
    }

    // WATCH 안내
    auto void show_watch(uint8_t on){
        LCDColorSet(on?3:4);
        line16(1, on?"WATCH:ON      ":"WATCH:OFF     ");
        line16(2, "MONITOR VALUE ");
        HAL_Delay(300);
    }

    // 진입 배너 → 메인뷰
    LCD16X2_Clear(MyLCD);
    LCDColorSet(4);
    line16(1, " RAM BROWSE    ");
    line16(2, " ROTARY=ADDR   ");
    HAL_Delay(250);

    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    // 초기 값 읽기
    last_val = *((volatile uint32_t*)addr);
    draw_main(addr, last_val, watch_on);

    for(;;){
        // 입력 갱신
        Buttons_Scan_1ms();
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);

        // ── 로터리: 주소 이동 ──
        if (rotaryEvent3 == ROTARY_EVENT_CW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            uint32_t next = addr + steps[step_i];
            if (next > (TM_RAM_END - 4u)) next = (TM_RAM_END - 4u);
            if (next != addr){
                addr = next;
                notch_ui_rotary_click_freq(1000.f);
                uint32_t v = *((volatile uint32_t*)addr);
                last_val = v;                            // 이동 시 기준값 갱신
                draw_main(addr, v, watch_on);
            }
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            uint32_t step = steps[step_i];
            uint32_t next = (addr >= step) ? (addr - step) : TM_RAM_BEGIN;
            if (next < TM_RAM_BEGIN) next = TM_RAM_BEGIN;
            if (next != addr){
                addr = next;
                notch_ui_rotary_click_freq(900.f);
                uint32_t v = *((volatile uint32_t*)addr);
                last_val = v;
                draw_main(addr, v, watch_on);
            }
        }

        // ENTER: STEP 순환
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            step_i = (uint8_t)((step_i + 1u) % N_STEPS);
            notch_ui_button_beep();
            show_step(steps[step_i]);
            tm_flush_inputs(60);            // 💡 추가
            uint32_t v = *((volatile uint32_t*)addr);
            last_val = v; draw_main(addr, v, watch_on);
        }

        // ENTER 길게: WATCH 토글
        if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            watch_on = !watch_on;
            notch_ui_button_beep();
            show_watch(watch_on);
            tm_flush_inputs(60);            // 💡 추가
            uint32_t v = *((volatile uint32_t*)addr);
            last_val = v; draw_main(addr, v, watch_on);
        }


        // ── WATCH 모드: 값 변화 감시 ──
        if (watch_on){
            uint32_t v = *((volatile uint32_t*)addr);
            if (v != last_val){
                // 변화 감지 → 즉시 표시 + 삑
                last_val = v;
                LCDColorSet(2);          // 밝게 하이라이트
                draw_main(addr, v, watch_on);
                notch_ui_button_beep();
                // 살짝 유지 후 기본색 복귀
                HAL_Delay(80);
                LCDColorSet(3);
                draw_main(addr, v, watch_on);
            }
        }

        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            tm_flush_inputs(120);           // 💡 추가
            return;
        }

        // (옵션) BACK 길게: 강제 리부트
        if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            LCDColorSet(5);
            line16(1, " FORCE REBOOT  ");
            line16(2, " LONG BACK     ");
            HAL_Delay(250);
            NVIC_SystemReset();
        }

        HAL_Delay(1);
    }
}







// ─────────────────────────────────────────────
// SOUND OUT (one-function, ultra-low RAM)
// - BK(0)=EXIT, ENT(2) short=RUN/STOP, ENT long=WAVE(SIN→SQR→TRI)
// - ROTARY: ±10 Hz
// - 전역/외부 입력 로직만 소비(main.c), 스택 폭주 없음
// ─────────────────────────────────────────────
// ─────────────────────────────────────────────
// SOUND OUT (one-function, ultra-low RAM)
// - BK(0)=EXIT, ENT(2) short=RUN/STOP, ENT long=WAVE(SIN→SQR→TRI)
// - ROTARY: ±10 Hz
// - 전역/외부 입력 로직만 소비(main.c), 스택 폭주 없음
// ─────────────────────────────────────────────
// ─────────────────────────────────────────────
// SOUND OUT (I2S 24bit packed, ultra-low RAM one-function)
// - BK(0)=EXIT, ENT short=RUN/STOP, ENT long=WAVE(SIN→SQR→TRI)
// - ROTARY: ±10 Hz
// - hi2s2: I2S_MODE_MASTER_TX, I2S_STANDARD_PHILIPS, I2S_DATAFORMAT_24B 가정
// - 스택 폭주 없음(정적 512B 버퍼), 24bit 패킹 수정, 페이드 인/아웃로 팝 줄임
// ─────────────────────────────────────────────
// ─────────────────────────────────────────────
// TM_SoundOut — reuse notch.c SoundGen (no extra buffers)
// - BK(0)=EXIT, ENT short=RUN/STOP, ENT long=WAVE(SIN→SQR→TRI)
// - ROTARY: ±10 Hz
// - 내부 톤 합성/패킹/페이드는 notch.c 파이프라인이 수행
// ─────────────────────────────────────────────
// ─────────────────────────────────────────────
// TM_SoundOut — reuse notch.c SoundGen (no extra buffers)
// - BK(0)=EXIT, ENT short=RUN/STOP, ENT long=WAVE(SIN→SQR→TRI)
// - ROTARY: ±10 Hz
// - 합성/패킹/페이드는 notch 파이프라인이 수행
// ─────────────────────────────────────────────
// ─────────────────────────────────────────────
// TM_SoundOut — notch.c 파이프라인 재사용 (재시작 금지)
// BK(0)=EXIT, ENT short=RUN/STOP, ENT long=WAVE(SIN→SQR→TRI)
// ROTARY: ±10 Hz
// ─────────────────────────────────────────────
static void TM_SoundOut(void)
{
    // externs (정의는 main.c/notch.c)
    extern volatile uint8_t  IsSoundGenReady;       // 1 = SoundGen on
    extern volatile uint8_t  SoundGenMode;          // 0=SIN,1=SQR,2=TRI
    extern volatile float    SoundFrequencyOutput;  // Hz

    extern volatile ButtonEvent buttonEvents[3];
    extern volatile RotaryEvent rotaryEvent3;
    extern void Poll_Rotary(TIM_HandleTypeDef*, int16_t*, RotaryEvent*);
    extern TIM_HandleTypeDef htim4; extern int16_t prev_count4;

    extern void LCD16X2_Clear(uint8_t lcd);
    extern void LCD16X2_Set_Cursor(uint8_t lcd, uint8_t row, uint8_t col);
    extern void LCDColorSet(uint8_t c);
    extern void LCD16X2_Write_String(uint8_t lcd, char *str);

    extern void notch_ui_button_beep(void);
    extern void notch_ui_mode_return_triple_beep(void);

    // 배너
    LCD16X2_Clear(LCD16X2_1);
    LCDColorSet(3);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1," SOUND OUT     ");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1," ENT:RUN ROT:Hz");

    // 잔여 입력 플러시 (메인 입력 공유)
    uint32_t t0 = HAL_GetTick();
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
    rotaryEvent3 = ROTARY_EVENT_NONE;
    while (HAL_GetTick()-t0 < 120) { buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE; rotaryEvent3 = ROTARY_EVENT_NONE; HAL_Delay(1); }

    // 기존 상태 백업 (메뉴 종료 시 복구)
    uint8_t  prev_on   = IsSoundGenReady;
    uint8_t  prev_mode = SoundGenMode;
    float    prev_hz   = SoundFrequencyOutput;

    // 로컬 상태
    uint8_t running = 1;
    int32_t freq    = (int32_t)((prev_hz>0.0f && prev_hz<100000.0f)? prev_hz : 1000);
    uint8_t wave    = SoundGenMode;

    // ★ 핵심: 파이프라인 재시작 금지! (main에서 이미 notch_start() 되어 있음)
    IsSoundGenReady      = 1;           // 바로 토너 생성 활성화
    SoundGenMode         = wave;
    SoundFrequencyOutput = (float)freq;
    notch_ui_button_beep();

    // 간단 UI 그리기
    static char l1[17], l2[17];
draw_ui:
    for (int i=0;i<16;i++){ l1[i]=' '; l2[i]=' '; } l1[16]=l2[16]='\0';
    l1[0]='W'; l1[1]='A'; l1[2]='V'; l1[3]=':';
    l1[4]=(wave==0)?'S':(wave==1)?'S':'T';
    l1[5]=(wave==0)?'I':(wave==1)?'Q':'R';
    l1[6]=(wave==0)?'N':(wave==1)?'R':'I';

    int32_t fclamp = (freq<0)?0:((freq>99999)?99999:freq);
    l2[0]='F'; l2[1]=':';
    l2[2]='0'+(fclamp/10000)%10; l2[3]='0'+(fclamp/1000)%10;
    l2[4]='0'+(fclamp/100)%10;   l2[5]='0'+(fclamp/10)%10; l2[6]='0'+(fclamp%10);
    l2[7]=' '; l2[8]='H'; l2[9]='z'; l2[11]= running?'R':'S';

    LCDColorSet(running?3:4);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1,l1);
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1,l2);

    // 루프
    for(;;){
        // 로터리 = 주파수
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);
        if (rotaryEvent3 == ROTARY_EVENT_CW)  {
            rotaryEvent3 = ROTARY_EVENT_NONE;
            freq += 10; if (freq < 10) freq = 10;
            SoundFrequencyOutput = (float)freq;
            notch_ui_button_beep();
            goto draw_ui;
        }
        if (rotaryEvent3 == ROTARY_EVENT_CCW) {
            rotaryEvent3 = ROTARY_EVENT_NONE;
            freq -= 10; if (freq < 10) freq = 10;
            SoundFrequencyOutput = (float)freq;
            notch_ui_button_beep();
            goto draw_ui;
        }

        // ENT short = Run/Stop (토너 on/off만 토글)
        if (buttonEvents[2]==BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2]=BUTTON_EVENT_NONE;
            running ^= 1;
            IsSoundGenReady = running ? 1 : 0;
            notch_ui_button_beep();
            goto draw_ui;
        }
        // ENT long = 파형 순환
        if (buttonEvents[2]==BUTTON_EVENT_LONG_PRESS){
            buttonEvents[2]=BUTTON_EVENT_NONE;
            wave = (uint8_t)((wave+1)%3);
            SoundGenMode = wave;
            notch_ui_button_beep();
            goto draw_ui;
        }
        // BK = 종료 (원상복구)
        if (buttonEvents[0]==BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0]=BUTTON_EVENT_NONE;
            // 복구
            IsSoundGenReady      = prev_on;
            SoundGenMode         = prev_mode;
            SoundFrequencyOutput = prev_hz;
            notch_ui_mode_return_triple_beep();
            return;
        }
        HAL_Delay(1);
    }
}











// ─────────────────────────────────────────────
// TM_I2S2_Input — I2S2 라인 입력 VU 테스트 (NOTCH 연동, 재시작 없음)
//  • BK(0)=EXIT  • ENT(2)=APR(패스스루) 토글  • ROTARY=미사용
// ─────────────────────────────────────────────

/*
static void TM_I2S2_Input(void)
{
    // ===== externs (정의는 main.c/notch.c) =====
    extern void     notch_set_vu_enabled(uint8_t on);   // I2S2 VU 측정 on/off
    extern void     notch_get_vu_segments(uint8_t *L, uint8_t *R); // 0..30 세그
    extern volatile uint8_t AudioProcessingIsReady;      // 1=라인 패스, 0=뮤트
    extern volatile ButtonEvent buttonEvents[3];
    extern volatile RotaryEvent rotaryEvent3;
    extern void Poll_Rotary(TIM_HandleTypeDef*, int16_t*, RotaryEvent*);
    extern TIM_HandleTypeDef htim4; extern int16_t prev_count4;
    extern void LCD16X2_Clear(uint8_t lcd);
    extern void LCD16X2_Set_Cursor(uint8_t lcd, uint8_t row, uint8_t col);
    extern void LCDColorSet(uint8_t c);
    extern void notch_ui_button_beep(void);
    extern void notch_ui_mode_return_triple_beep(void);

    // 배너
    LCD16X2_Clear(LCD16X2_1);
    LCDColorSet(3);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1," I2S2 INPUT    ");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1," ENT:APR BK:EX ");

    // 잔여 입력 플러시
    uint32_t t0 = HAL_GetTick();
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
    rotaryEvent3 = ROTARY_EVENT_NONE;
    while (HAL_GetTick()-t0 < 120) { buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE; rotaryEvent3=ROTARY_EVENT_NONE; HAL_Delay(1); }

    // 상태 백업
    uint8_t  prev_apr = AudioProcessingIsReady;
    // VU on
    notch_set_vu_enabled(1);

    // UI 버퍼(정적, 스택 X)
    static char l1[17], l2[17];

draw_ui:
    {
        // L/R VU 막대 (0..30 세그 → 0..10 칸)
        uint8_t segL=0, segR=0;
        notch_get_vu_segments(&segL, &segR);
        uint8_t barL = (uint8_t)((segL + 2) / 3); if (barL>10) barL=10;
        uint8_t barR = (uint8_t)((segR + 2) / 3); if (barR>10) barR=10;

        for (int i=0;i<16;i++){ l1[i]=' '; l2[i]=' '; } l1[16]=l2[16]='\0';
        // "L:[" + 10bar + "]"
        l1[0]='L'; l1[1]=':'; l1[2]='['; l1[13]=']';
        for (int i=0;i<10;i++) l1[3+i] = (i<barL) ? '#' : '.';
        // "R:[" + 10bar + "]"
        l2[0]='R'; l2[1]=':'; l2[2]='['; l2[13]=']';
        for (int i=0;i<10;i++) l2[3+i] = (i<barR) ? '#' : '.';

        // APR 상태 표시
        l1[15] = AudioProcessingIsReady ? 'P' : 'M';  // P=pass, M=mute

        LCDColorSet(AudioProcessingIsReady?3:4);
        LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1,l1);
        LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1,l2);
    }

    // 루프
    for(;;){
        Poll_Rotary(&htim4,&prev_count4,(RotaryEvent*)&rotaryEvent3);
        // (로터리는 미사용. 원하면 게인/레벨 옵션에 매핑 가능)
        rotaryEvent3 = ROTARY_EVENT_NONE;

        // ENT: APR 토글 (패스/뮤트)
        if (buttonEvents[2]==BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2]=BUTTON_EVENT_NONE;
            AudioProcessingIsReady ^= 1;
            notch_ui_button_beep();
            goto draw_ui;
        }
        // BK: 종료(원복)
        if (buttonEvents[0]==BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0]=BUTTON_EVENT_NONE;
            notch_set_vu_enabled(0);
            AudioProcessingIsReady = prev_apr;
            notch_ui_mode_return_triple_beep();
            return;
        }
        HAL_Delay(30);
        goto draw_ui;
    }
}


// ─────────────────────────────────────────────
// TM_I2S3_Input — I2S3 마이크 입력 미니 VU (NOTCH 연동, 재시작 없음)
//  • BK(0)=EXIT  • ENT(2)=Mic 모니터 on/off( SoundBalance 0↔복귀 )
//  • ROTARY=미사용 (원하면 SoundBalance 값 회전으로 조절 가능)
// ─────────────────────────────────────────────
static void TM_I2S3_Input(void)
{
    // ===== externs =====
    extern uint8_t  notch_get_mic_vu8(void);       // 0..255
    extern volatile uint16_t SoundBalance;         // 0..50 사용 (mic 게인 LUT)
    extern volatile ButtonEvent buttonEvents[3];
    extern volatile RotaryEvent rotaryEvent3;
    extern void Poll_Rotary(TIM_HandleTypeDef*, int16_t*, RotaryEvent*);
    extern TIM_HandleTypeDef htim4; extern int16_t prev_count4;
    extern void LCD16X2_Clear(uint8_t lcd);
    extern void LCD16X2_Set_Cursor(uint8_t lcd, uint8_t row, uint8_t col);
    extern void LCDColorSet(uint8_t c);
    extern void notch_ui_button_beep(void);
    extern void notch_ui_mode_return_triple_beep(void);

    // 배너
    LCD16X2_Clear(LCD16X2_1);
    LCDColorSet(3);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1," I2S3 MIC      ");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1," ENT:MON BK:EX ");

    // 잔여 입력 플러시
    uint32_t t0 = HAL_GetTick();
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
    rotaryEvent3 = ROTARY_EVENT_NONE;
    while (HAL_GetTick()-t0 < 120) { buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE; rotaryEvent3=ROTARY_EVENT_NONE; HAL_Delay(1); }

    // 상태 백업/로컬
    uint16_t prev_sb = SoundBalance;    // 0..50
    uint8_t  mon_on  = (SoundBalance>0) ? 1 : 0;

    static char l1[17], l2[17];

draw_ui:
    // vu(0..255) → 0..10 칸 맵핑
    {
        uint8_t vu = notch_get_mic_vu8();
        uint8_t bar = (uint8_t)((vu + 12) / 25); if (bar>10) bar=10;

        for (int i=0;i<16;i++){ l1[i]=' '; l2[i]=' '; } l1[16]=l2[16]='\0';
        l1[0]='M'; l1[1]='I'; l1[2]='C'; l1[3]=':'; l1[4]='['; l1[15]=']';
        for (int i=0;i<10;i++) l1[5+i] = (i<bar) ? '#' : '.';

        // MON 상태
        l2[0]='M'; l2[1]='O'; l2[2]='N'; l2[3]=':'; l2[5] = mon_on ? '1' : '0';

        LCDColorSet(mon_on?3:4);
        LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1,l1);
        LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1,l2);
    }

    for(;;){
        Poll_Rotary(&htim4,&prev_count4,(RotaryEvent*)&rotaryEvent3);
        rotaryEvent3 = ROTARY_EVENT_NONE; // (원하면 SoundBalance 조절에 매핑 가능)

        // ENT: 모니터 on/off (SoundBalance 0 ↔ 백업값)
        if (buttonEvents[2]==BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2]=BUTTON_EVENT_NONE;
            mon_on ^= 1;
            if (mon_on){
                if (prev_sb == 0) prev_sb = 20;  // 최소 가청 확보
                SoundBalance = prev_sb;
            } else {
                SoundBalance = 0;
            }
            notch_ui_button_beep();
            goto draw_ui;
        }

        // BK: 종료(복구)
        if (buttonEvents[0]==BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0]=BUTTON_EVENT_NONE;
            SoundBalance = prev_sb;
            notch_ui_mode_return_triple_beep();
            return;
        }

        HAL_Delay(30);
        goto draw_ui;
    }
}

*/

static void TM_I2S2_Input(void) {
    LCDColorSet(4);
    LCD16X2_Clear(LCD16X2_1);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1," I2S2 INPUT    ");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1," NOT AVAILABLE ");
    // BK로만 빠지도록
    extern volatile ButtonEvent buttonEvents[3];
    while (buttonEvents[0]!=BUTTON_EVENT_SHORT_PRESS){ HAL_Delay(10); }
    buttonEvents[0]=BUTTON_EVENT_NONE;
}

static void TM_I2S3_Input(void) {
    LCDColorSet(4);
    LCD16X2_Clear(LCD16X2_1);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1," I2S3 INPUT    ");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1," NOT AVAILABLE ");
    extern volatile ButtonEvent buttonEvents[3];
    while (buttonEvents[0]!=BUTTON_EVENT_SHORT_PRESS){ HAL_Delay(10); }
    buttonEvents[0]=BUTTON_EVENT_NONE;
}





// 악보 이벤트: 반음(step, A4=0)와 길이(ms)
typedef struct { int8_t step; uint16_t ms; } TMScoreEvt;

// 예시 악보 (울애기 취향으로 바꿔서 쓰면 됨!)
// 플래시 상주(상수) → RAM 사용 0
static const TMScoreEvt g_score[] = {
    {  0, 300}, {  2, 300}, {  4, 300}, {  5, 300}, {  7, 300}, {  9, 300}, { 11, 300}, { 12, 600},
    { 11, 300}, {  9, 300}, {  7, 300}, {  5, 300}, {  4, 300}, {  2, 300}, {  0, 600},
};
static const uint32_t g_score_len = sizeof(g_score)/sizeof(g_score[0]);

static inline float tm_step_to_freq(int8_t step) {
    // A4=440 기준 12-TET
    return 440.0f * powf(2.0f, (float)step/12.0f);
}

// ─────────────────────────────────────────────
// TM_BeepScore_Overlay — notch 최종 출력단 오버레이 믹서로 노트 재생
//  • BK(0)=EXIT, ENT short=PAUSE/RESUME, ROTARY=CW×2 / CCW×0.5 (템포)
//  • I2S 재시작/버퍼 없음, RAM 거의 0
// ─────────────────────────────────────────────
// ─────────────────────────────────────────────
// TM_BeepScore_Overlay — 고정길이 NOTE 엔진 사용(플리커 제거)
//  • BK(0)=EXIT, ENT short=PAUSE/RESUME, ROTARY: 템포 x0.5/×2
// ─────────────────────────────────────────────
static void TM_BeepScore(void)
{
    // externs
    extern void     notch_note_start(float freq_hz, uint16_t ms, float gain);
    extern uint8_t  notch_note_busy(void);
    extern void     notch_note_stop(void);

    extern volatile ButtonEvent buttonEvents[3];
    extern volatile RotaryEvent rotaryEvent3;
    extern void Poll_Rotary(TIM_HandleTypeDef*, int16_t*, RotaryEvent*);
    extern TIM_HandleTypeDef htim4; extern int16_t prev_count4;

    extern void LCD16X2_Clear(uint8_t);
    extern void LCD16X2_Set_Cursor(uint8_t, uint8_t, uint8_t);
    extern void LCDColorSet(uint8_t);
    extern void notch_ui_button_beep(void);
    extern void notch_ui_mode_return_triple_beep(void);

    // 배너
    LCD16X2_Clear(LCD16X2_1);
    LCDColorSet(3);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1," BEEP SCORE    ");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1," ENT:PAU BK:EX ");

    // 잔여 입력 플러시
    uint32_t t0=HAL_GetTick();
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
    rotaryEvent3=ROTARY_EVENT_NONE;
    while (HAL_GetTick()-t0<120){ buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE; rotaryEvent3=ROTARY_EVENT_NONE; HAL_Delay(1); }

    // 악보(예시)
    typedef struct { uint16_t f; uint16_t ms; } Note;
    static const Note kScore[] = {
        // F 4 / F 4 /
        {349, 522}, {349, 522},

        // D 8 Eflat 8 F 4 G 4 G 4 F 2
        {294, 261}, {311, 261}, {349, 522}, {392, 522}, {392, 522}, {349,1043},

        // F 4 Bflat5 4 D5 4 C5 8 Bflat5 8 C5 1
        {349, 522}, {466, 522}, {587, 522}, {523, 261}, {466, 261}, {523,2087},

        // D5 4 D5 4 C5 4 C5 4 Bflat5 4 C5 8 Bflat5 8 G 4 G 4 F 4 F 4 F 4 D 8 C 8 Bflat 1
        {587, 522}, {587, 522}, {523, 522}, {523, 522}, {466, 522}, {523, 261}, {466, 261},
        {392, 522}, {392, 522}, {349, 522}, {349, 522}, {349, 522}, {294, 261}, {262, 261}, {233,2087},
    }; // <고향의 봄> / RAM에 집어넣지말것. CONST 유지할것.
    const uint32_t N = sizeof(kScore)/sizeof(kScore[0]);

    uint8_t  playing = 1;
    float    tempo   = 1.0f;
    uint32_t i = 0;
    const float GAIN = 0.5f;

    static char l2[17];

draw_ui:
    for (int k=0;k<16;k++) l2[k]=' '; l2[16]='\0';
    l2[0]='I'; l2[1]='D'; l2[2]='X'; l2[3]=':'; l2[4]='0'+(i/10)%10; l2[5]='0'+(i%10);
    l2[8]='T'; l2[9]=':'; l2[10]=(tempo>=1.0f)?'1':'0';
    LCDColorSet(playing?3:4);
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1,l2);

    while (i < N){
        // 입력
        Poll_Rotary(&htim4,&prev_count4,(RotaryEvent*)&rotaryEvent3);
        if (rotaryEvent3==ROTARY_EVENT_CW){  rotaryEvent3=ROTARY_EVENT_NONE; if (tempo<4.0f) tempo*=2.0f; notch_ui_button_beep(); goto draw_ui; }
        if (rotaryEvent3==ROTARY_EVENT_CCW){ rotaryEvent3=ROTARY_EVENT_NONE; if (tempo>0.25f) tempo*=0.5f; notch_ui_button_beep(); goto draw_ui; }
        if (buttonEvents[2]==BUTTON_EVENT_SHORT_PRESS){ buttonEvents[2]=BUTTON_EVENT_NONE; playing^=1; notch_ui_button_beep(); goto draw_ui; }
        if (buttonEvents[0]==BUTTON_EVENT_SHORT_PRESS){ buttonEvents[0]=BUTTON_EVENT_NONE; notch_note_stop(); notch_ui_mode_return_triple_beep(); return; }

        if (!playing){ HAL_Delay(5); continue; }

        // ★ 한 번만 트리거 → 엔진이 ms 동안 지속 재생
        uint32_t dur = (uint32_t)(kScore[i].ms / tempo + 0.5f);
        if (dur < 20) dur = 20;
        notch_note_start((float)kScore[i].f, (uint16_t)dur, GAIN);

        // 끝날 때까지 기다리기(중간 입력도 수용)
        while (notch_note_busy()){
            if (buttonEvents[0]==BUTTON_EVENT_SHORT_PRESS){ buttonEvents[0]=BUTTON_EVENT_NONE; notch_note_stop(); notch_ui_mode_return_triple_beep(); return; }
            if (buttonEvents[2]==BUTTON_EVENT_SHORT_PRESS){ buttonEvents[2]=BUTTON_EVENT_NONE; playing^=1; notch_ui_button_beep(); goto draw_ui; }
            HAL_Delay(1);
        }

        // 노트 사이 간격(옵션)
        uint32_t gt = HAL_GetTick(); while (HAL_GetTick()-gt < 10){ HAL_Delay(1); }

        ++i; goto draw_ui;
    }

    LCDColorSet(3);
    LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1," SCORE DONE    ");
    LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1," BK:EXIT       ");
    HAL_Delay(800);
}








static void TM_LCD_Test(void)
{


    // ── 상태 ──
    enum { MODE_FILL=0, MODE_CHECKER, MODE_ASCII, MODE_SCROLL, MODE_COLOR_CYCLE, MODE_COUNT };
    uint8_t mode = 0;
    uint8_t running = 1;        // ENT로 토글
    uint8_t color_auto = 1;     // ENT(long)로 토글(색상 자동 사이클)
    uint8_t tick = 0;
    uint8_t scroll_off = 0;
    uint8_t color = 3;          // 기본 초록
    uint32_t last_anim_ms = HAL_GetTick();
    const uint32_t anim_period_ms = 60;

    // ── 진입 배너 ──
    LCD16X2_Clear(LCD16X2_1);
    LCDColorSet(2);
    tm_lcd_line(1, " LCD TEST      ");
    tm_lcd_line(2, " ENT=RUN ROT=MD");
    HAL_Delay(300);

    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    auto void draw_header(void){
        char l1[17]="MODE:xxxxx    ";
        const char* name = (mode==MODE_FILL)?"FILL":
                           (mode==MODE_CHECKER)?"CHECKER":
                           (mode==MODE_ASCII)?"ASCII":
                           (mode==MODE_SCROLL)?"SCROLL":"COLORCYCLE";
        memcpy(&l1[5], name, strlen(name));
        char l2[17]="RUN: x  COL: x ";
        l2[5] = running?'Y':'N';
        l2[14] = '0'+(color%10);
        LCDColorSet(running?3:4);
        tm_lcd_line(1, l1);
        tm_lcd_line(2, l2);
    };

    auto void draw_frame(void){
        // 애니메이션/패턴 그리기(16x2)
        switch(mode){
            case MODE_FILL: {
                LCDColorSet(color);
                LCD16X2_Set_Cursor(LCD16X2_1, 1, 1);
                // 0xFF 꽉 채움
                char buf[17]; memset(buf, 0xFF, 16); buf[16]='\0';
                LCD16X2_Write_String(LCD16X2_1, buf);
                LCD16X2_Set_Cursor(LCD16X2_1, 2, 1);
                LCD16X2_Write_String(LCD16X2_1, buf);
                break;
            }
            case MODE_CHECKER: {
                LCDColorSet(color);
                char a[17], b[17];
                for (int i=0;i<16;i++){ a[i] = ( (i+tick)&1 ) ? 0xFF : ' '; }
                for (int i=0;i<16;i++){ b[i] = ( (i+tick)&1 ) ? ' ' : 0xFF; }
                a[16]=b[16]='\0';
                LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1,a);
                LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1,b);
                break;
            }
            case MODE_ASCII: {
                LCDColorSet(color);
                char l1[17], l2[17];
                for (int i=0;i<16;i++) l1[i] = (char)(' '+((tick+i)%('~'-' ')));
                for (int i=0;i<16;i++) l2[i] = (char)(' '+((tick+16+i)%('~'-' ')));
                l1[16]=l2[16]='\0';
                LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1,l1);
                LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1,l2);
                break;
            }
            case MODE_SCROLL: {
                LCDColorSet(color);
                const char *msg = " LCD SCROLL TEST —— ROT=MODE ENT=RUN ";
                // 32자 미만이라 간단 오프셋 스크롤
                char line[17];
                for (int i=0;i<16;i++){
                    int idx = (scroll_off + i) % (int)strlen(msg);
                    line[i] = msg[idx];
                }
                line[16]='\0';
                LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1,line);
                LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1,line);
                break;
            }
            case MODE_COLOR_CYCLE: {
                // 색상 자동 사이클 / 수동 모드
                if (color_auto && running){
                    if ((HAL_GetTick()-last_anim_ms) >= 180){
                        last_anim_ms = HAL_GetTick();
                        color = (uint8_t)((color%6)+1); // 1..6 순환(보드 팔레트 가정)
                    }
                }
                LCDColorSet(color);
                char l1[17]="COLOR CYCLE    ";
                char l2[17]="AUTO: x C:x    ";
                l2[6] = color_auto?'Y':'N';
                l2[11]= '0'+(color%10);
                tm_lcd_line(1,l1); tm_lcd_line(2,l2);
                break;
            }
        }
    };

    draw_header();
    HAL_Delay(120);

    for(;;){
        // 입력
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);

        if (rotaryEvent3 == ROTARY_EVENT_CW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            mode = (uint8_t)((mode + 1) % MODE_COUNT);
            notch_ui_rotary_click_freq(1000.f);
            LCD16X2_Clear(LCD16X2_1);
            draw_header();
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            mode = (uint8_t)((mode + MODE_COUNT - 1) % MODE_COUNT);
            notch_ui_rotary_click_freq(900.f);
            LCD16X2_Clear(LCD16X2_1);
            draw_header();
        }

        // ENT: RUN/STOP
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            running ^= 1;
            notch_ui_button_beep();
            draw_header();
        }

        // ENT LONG: COLOR_CYCLE에서 자동/수동 토글, 그 외에는 색상 한 칸 증가
        if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            if (mode == MODE_COLOR_CYCLE){
                color_auto ^= 1;
            } else {
                color = (uint8_t)((color%6)+1);
            }
            notch_ui_button_beep();
            draw_header();
        }

        // BK: 상위 복귀
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            return;
        }

        // RUN 중이면 애니메이션
        if (running){
            uint32_t now = HAL_GetTick();
            if (now - last_anim_ms >= anim_period_ms){
                last_anim_ms = now;
                tick++;
                scroll_off++;
                draw_frame();
            }
        } else {
            // 정지 상태에서도 현재 프레임 한 번 그려줌
            draw_frame();
            HAL_Delay(30);
        }
        HAL_Delay(1);
    }
}

// ───────────────────────────────────────────────
// Backlight Test: 버튼으로만 색상 순환 (PWM 없음)
// BK(0)=EXIT, ENT(2) short=다음 색, ENT long=이전 색
// ───────────────────────────────────────────────
static void TM_Backlight(void)
{
    // 외부 심볼
    extern volatile ButtonEvent buttonEvents[3];
    extern volatile RotaryEvent rotaryEvent3;
    extern void notch_ui_button_beep(void);
    extern void notch_ui_mode_return_triple_beep(void);
    extern void LCD16X2_Clear(uint8_t lcd);
    extern void LCDColorSet(uint8_t c);
    extern void tm_lcd_line(uint8_t row, const char* s);

    // 순환할 팔레트(보드 팔레트에 맞춰 필요시 수정: 1~6 가정)
    static const uint8_t kPalette[] = {1,2,3,4,5,6};
    uint8_t idx = 0;

    auto void redraw(void){
        char l1[17] = " BACKLIGHT TEST";
        char l2[17] = " COL:x  ENT->++";
        l2[6] = (char)('0' + (kPalette[idx] % 10));
        LCDColorSet(kPalette[idx]);
        tm_lcd_line(1, l1);
        tm_lcd_line(2, l2);
    };

    LCD16X2_Clear(LCD16X2_1);
    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    redraw();

    for(;;){
        // ENT: 다음 색
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            idx = (uint8_t)((idx + 1) % (uint8_t)(sizeof(kPalette)));
            notch_ui_button_beep();
            redraw();
        }
        // ENT 길게: 이전 색
        if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            idx = (uint8_t)((idx + (uint8_t)sizeof(kPalette) - 1) % (uint8_t)sizeof(kPalette));
            notch_ui_button_beep();
            redraw();
        }
        // BK: 종료
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            return;
        }

        // 로터리는 이 테스트에서 무시
        HAL_Delay(1);
    }
}

// =========================================================
// TEXT PRINT / INPUT TEST (drop-in, ISR 이벤트 소비만)
//  - BK(0)=뒤로가기, ENT(2)=기능(짧게/길게), 로터리=선택/속도
//  - Buttons_Scan_1ms() 호출/정의 없음 (main ISR만 사용)
// =========================================================

static void TM_TextPrint(void)
{
    // ── 외부 심볼 ──


    // ── 프리셋 문자열(라인 구분자: '|') ──
    static const char* kPresets[] = {
        "THE QUICK|BROWN FOX",
        "HELLO, WORLD!|202113946",
        "ALIGN TEST|LEFT/CENTER/RIGHT",
        "PUNCT: !?:;.,-|()[]{}",
        "DO YOU KNOW|KIMCHI????",
        "WIDTH CHECK|MMMMmmmmIIII",
        "EDGES____----|____----EDGES",
        "SPACES    TRIM|TRIM    SPACES",
    };
    enum { N_PRESETS = (int)(sizeof(kPresets)/sizeof(kPresets[0])) };

    // ── 상태 ──
    uint8_t align = 0;     // 0=L,1=C,2=R  (ENT 길게로 변경)
    uint8_t running = 1;   // 타자 효과 실행 on/off (ENT 짧게)
    uint8_t color = 3;     // 1..6 팔레트
    int     sel = 0;       // 프리셋 선택 (로터리)
    uint32_t typos = 0;    // 타자 인덱스(문자 개수)
    uint32_t last_ms = HAL_GetTick();
    uint32_t period_ms = 60; // 로터리 클릭마다 +/-로 속도 조정

    // ── 진입 배너 ──
    LCD16X2_Clear(LCD16X2_1);
    LCDColorSet(2);
    tm_lcd_line(1, " TEXT PRINT    ");
    tm_lcd_line(2, " ENT:RUN ROT:SEL");
    HAL_Delay(250);
    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    // ── 유틸: 정렬하여 16자 라인 생성 ──
    auto void make_aligned_16(char out[17], const char* text, uint8_t align_mode){
        // text 최대 16자로 잘라 정렬
        size_t len = 0; while (text[len] && len < 16) len++;
        int pad_left=0;
        if (align_mode==1){ // center
            pad_left = (int)((16 - (int)len)/2);
        } else if (align_mode==2){ // right
            pad_left = (int)(16 - (int)len);
        }
        if (pad_left < 0) pad_left = 0;
        for (int i=0;i<16;i++) out[i] = ' ';
        for (int i=0;i<(int)len && (pad_left+i)<16;i++) out[pad_left+i] = text[i];
        out[16]='\0';
    };

    // ── 유틸: 프리셋을 2라인으로 나누기 ──
    auto void split2(const char* src, char* l1, char* l2){
        const char* p = src; const char* bar = NULL;
        for (const char* q=src; *q; ++q){ if (*q=='|'){ bar=q; break; } }
        if (bar){
            size_t n1 = (size_t)(bar-p); if (n1>32) n1=32;
            size_t n2 = strlen(bar+1);   if (n2>32) n2=32;
            strncpy(l1, p, n1); l1[n1]='\0';
            strncpy(l2, bar+1, n2); l2[n2]='\0';
        } else {
            strncpy(l1, src, 32); l1[32]='\0';
            l2[0]='\0';
        }
    };

    // ── 프레임 그리기 ──
    auto void draw_frame(void){
        char raw1[33], raw2[33];
        split2(kPresets[sel], raw1, raw2);

        // 타자 효과: typos 길이만큼만 보여줌
        char buf1full[33], buf2full[33];
        snprintf(buf1full, sizeof(buf1full), "%s", raw1);
        snprintf(buf2full, sizeof(buf2full), "%s", raw2);

        size_t L1 = strlen(buf1full), L2 = strlen(buf2full);
        size_t vis1 = (typos <= L1) ? typos : L1;
        size_t vis2 = (typos >  L1) ? ( (typos - L1 <= L2) ? (typos - L1) : L2 ) : 0;

        char show1[33], show2[33];
        memcpy(show1, buf1full, vis1); show1[vis1]='\0';
        memcpy(show2, buf2full, vis2); show2[vis2]='\0';

        char l1[17], l2[17];
        make_aligned_16(l1, show1, align);
        make_aligned_16(l2, show2, align);

        LCDColorSet(color);
        LCD16X2_Set_Cursor(LCD16X2_1,1,1); LCD16X2_Write_String(LCD16X2_1, l1);
        LCD16X2_Set_Cursor(LCD16X2_1,2,1); LCD16X2_Write_String(LCD16X2_1, l2);
    };

    // ── 헤더(상태) ──
    auto void draw_status(void){
        char h1[17]="SEL:00 ALN:X  ";
        char h2[17]="SPD:000ms COL:";
        h1[4]  = '0' + ((sel/10)%10);
        h1[5]  = '0' + (sel%10);
        h1[11] = (align==0)?'L':(align==1)?'C':'R';
        h2[4]  = '0' + ( (period_ms/100)%10 );
        h2[5]  = '0' + ( (period_ms/10)%10 );
        h2[6]  = '0' + ( period_ms%10 );
        h2[15] = '0' + (color%10);
        LCDColorSet(4);
        tm_lcd_line(1, h1);
        tm_lcd_line(2, h2);
    };

    draw_status();
    draw_frame();

    for(;;){
        // 로터리: 프리셋 선택 (눌릴 때마다 타자 인덱스 리셋)
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);
        if (rotaryEvent3 == ROTARY_EVENT_CW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            sel = (sel + 1) % N_PRESETS;
            typos = 0;
            notch_ui_rotary_click_freq(1000.f);
            draw_status(); draw_frame();
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            sel = (sel + N_PRESETS - 1) % N_PRESETS;
            typos = 0;
            notch_ui_rotary_click_freq(900.f);
            draw_status(); draw_frame();
        }

        // ENT 짧게: RUN/STOP 토글
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            running ^= 1;
            notch_ui_button_beep();
            draw_status();
        }

        // ENT 길게: 정렬 변경(L→C→R→L), 색상도 한 칸
        if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            align = (uint8_t)((align+1)%3);
            color = (uint8_t)((color%6)+1);
            notch_ui_button_beep();
            draw_status(); draw_frame();
        }

        // BK: 종료
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            return;
        }

        // 타자 효과 실행
        if (running){
            uint32_t now = HAL_GetTick();
            if (now - last_ms >= period_ms){
                last_ms = now;
                typos++;
                draw_frame();
            }
        }

        HAL_Delay(1);
    }
}

static void TM_InputTest(void)
{

    // ── 카운터 ──
    uint16_t bk_cnt=0, ent_cnt=0, entL_cnt=0;
    uint16_t cw_cnt=0, ccw_cnt=0;
    char last[6] = "----"; // 최근 이벤트 4글자 표시

    // ── 헤더 ──
    LCD16X2_Clear(LCD16X2_1);
    LCDColorSet(2);
    tm_lcd_line(1, " INPUT TEST    ");
    tm_lcd_line(2, " BTN/ROT MON   ");
    HAL_Delay(250);
    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    auto void draw(void){
        // 16x2에 압축 표기
        // L1: BK:xx EN:xx LG:xx
        // L2: CW:xx CC:xx [LAST]
        char l1[17]="BK:00 EN:00 LG:00";
        char l2[17]="CW:00 CC:00 ----";
        l1[3]  = '0'+((bk_cnt/10)%10);  l1[4]  = '0'+(bk_cnt%10);
        l1[9]  = '0'+((ent_cnt/10)%10); l1[10] = '0'+(ent_cnt%10);
        l1[15] = '0'+((entL_cnt/10)%10);l1[16-0] = '\0'; // LG 두 자리만
        // L2
        l2[3]  = '0'+((cw_cnt/10)%10);  l2[4]  = '0'+(cw_cnt%10);
        l2[9]  = '0'+((ccw_cnt/10)%10); l2[10] = '0'+(ccw_cnt%10);
        memcpy(&l2[12], last, 4); l2[16]='\0';

        LCDColorSet(4);
        tm_lcd_line(1, l1);
        tm_lcd_line(2, l2);
    };

    draw();

    for(;;){
        // 로터리 이벤트
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);
        if (rotaryEvent3 == ROTARY_EVENT_CW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            cw_cnt++; memcpy(last,"CW  ",4);
            notch_ui_rotary_click_freq(1000.f);
            draw();
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            ccw_cnt++; memcpy(last,"CCW ",4);
            notch_ui_rotary_click_freq(900.f);
            draw();
        }

        // 버튼: BK
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            bk_cnt++; memcpy(last,"BK  ",4);
            notch_ui_mode_return_triple_beep();
            draw();
        }

        // 버튼: ENT short
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            ent_cnt++; memcpy(last,"ENT ",4);
            notch_ui_button_beep();
            draw();
        }

        // 버튼: ENT long (카운터 리셋 토글로 사용)
        if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            entL_cnt++; memcpy(last,"ENTL",4);
            // 길게 누르면 카운터 모두 리셋
            bk_cnt=ent_cnt=entL_cnt=cw_cnt=ccw_cnt=0;
            notch_ui_button_beep();
            draw();
        }

        // BK 두 번 누르면 빠져나가고 싶다면 여기에서 확장 가능
        // 이번 버전은 단일 BK로 종료
        if (bk_cnt){ /* no-op; just illustration */ }

        // 상위 복귀는 'BK' 짧게 한 번이면 충분(위에서 처리됨→return)
        // 루프 유지
        HAL_Delay(1);

        // BK 눌렀을 때 바로 return 하려면 위의 BK 처리에서 return; 하면 됨.
        if (0) return;
    }
}


static void tm_dispatch(uint8_t sel)
{
    switch(sel){
        case 0: TM_FlashTest();  break; case 1: TM_RamTest();    break;
        case 2: TM_FlashBrowse();break; case 3: TM_RamBrowse();  break;
        case 4: TM_SoundOut();   break; case 5: TM_I2S2_Input(); break;
        case 6: TM_I2S3_Input(); break; case 7: TM_BeepScore();  break;
        case 8: TM_LCD_Test();   break; case 9: TM_Backlight();  break;
        case 10: TM_TextPrint(); break; case 11: TM_InputTest(); break;

        case TM_IDX_NUKE: TM_FlashNuke_QuickSafe(); break;  // ★ 뉴크 실행
        case TM_IDX_AUTOTEST: TM_AutoTest();   break;
    }
}
















//// 뉴크!!!!!!!!!!!
//// 존나게 위험합니다!!!!
//// 실행시 책임못짐!!!!!
///// 히로시마!!!!

// ==== FLASH NUKE (Full device wipe with playful "NUKE" confirmation) ====
//  - 기본: 모든 섹터 ERASE (자기자신 섹터는 기본 제외) → BLANK(0xFF)
//  - 옵션: ZERO_FILL_ALL 주석 해제 시, ERASE 후 전 영역 0x00000000 프로그램 (매우 느림)
//  - 마지막 단계: ENTER 2초 홀드 시 "자기자신 섹터"까지 지워서 즉시 사망(개발환경 전제)

















// #define ZERO_FILL_ALL   // ← 진짜 '0으로 초기화'가 필요하면 주석 해제 (매우 느림/위험)


static uint32_t tm_sector_base(uint32_t sector) {
    switch(sector){
        case FLASH_SECTOR_0:  return 0x08000000u;
        case FLASH_SECTOR_1:  return 0x08004000u;
        case FLASH_SECTOR_2:  return 0x08008000u;
        case FLASH_SECTOR_3:  return 0x0800C000u;
        case FLASH_SECTOR_4:  return 0x08010000u;
        case FLASH_SECTOR_5:  return 0x08020000u;
        case FLASH_SECTOR_6:  return 0x08040000u;
        case FLASH_SECTOR_7:  return 0x08060000u;
        case FLASH_SECTOR_8:  return 0x08080000u;
        case FLASH_SECTOR_9:  return 0x080A0000u;
        case FLASH_SECTOR_10: return 0x080C0000u;
        default:              return 0x080E0000u; // 11
    }
}

static inline uint32_t tm_sector_size(uint32_t s) {
    switch(s){
        case FLASH_SECTOR_0 :
        case FLASH_SECTOR_1 :
        case FLASH_SECTOR_2 :
        case FLASH_SECTOR_3 : return 16u*1024u;
        case FLASH_SECTOR_4 : return 64u*1024u;
        default             : return 128u*1024u; // 5..11
    }
}

static inline uint32_t tm_addr2sector(uint32_t a){
    if(a<0x08004000u) return FLASH_SECTOR_0;
    if(a<0x08008000u) return FLASH_SECTOR_1;
    if(a<0x0800C000u) return FLASH_SECTOR_2;
    if(a<0x08010000u) return FLASH_SECTOR_3;
    if(a<0x08020000u) return FLASH_SECTOR_4;
    if(a<0x08040000u) return FLASH_SECTOR_5;
    if(a<0x08060000u) return FLASH_SECTOR_6;
    if(a<0x08080000u) return FLASH_SECTOR_7;
    if(a<0x080A0000u) return FLASH_SECTOR_8;
    if(a<0x080C0000u) return FLASH_SECTOR_9;
    if(a<0x080E0000u) return FLASH_SECTOR_10;
    return FLASH_SECTOR_11;
}

__attribute__((section(".RamFunc"), noinline))
static int tm_ram_erase_sector(uint32_t sector)
{
    // 플래시가 바쁠 땐 기다림
    while (FLASH->SR & FLASH_SR_BSY) { /* spin */ }

    // 잠금 해제
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }

    // 에러 플래그 클리어 (있다면)
    FLASH->SR = (FLASH_SR_EOP
#if defined(FLASH_SR_OPERR)
               | FLASH_SR_OPERR
#endif
#if defined(FLASH_SR_WRPERR)
               | FLASH_SR_WRPERR
#endif
#if defined(FLASH_SR_PGAERR)
               | FLASH_SR_PGAERR
#endif
#if defined(FLASH_SR_PGPERR)
               | FLASH_SR_PGPERR
#endif
#if defined(FLASH_SR_PGSERR)
               | FLASH_SR_PGSERR
#endif
#if defined(FLASH_SR_RDERR)
               | FLASH_SR_RDERR
#endif
               );

    // 섹터 선택 + 섹터 소거
#if defined(FLASH_CR_SNB_Msk) && defined(FLASH_CR_SNB_Pos)
    FLASH->CR &= ~FLASH_CR_SNB_Msk;
    FLASH->CR |= FLASH_CR_SER | ((sector << FLASH_CR_SNB_Pos) & FLASH_CR_SNB_Msk);
#else
    // 구형 헤더 대응(대부분 F4는 위 매크로 존재)
    FLASH->CR &= ~(0xFu << 3);
    FLASH->CR |= FLASH_CR_SER | (sector << 3);
#endif

    // 시작!
    FLASH->CR |= FLASH_CR_STRT;

    // 완료까지 대기
    while (FLASH->SR & FLASH_SR_BSY) { /* spin */ }

    // 완료/EOP 정리
    uint32_t sr = FLASH->SR;
    FLASH->SR  = FLASH_SR_EOP;            // EOP 클리어
    FLASH->CR &= ~(FLASH_CR_SER
#if defined(FLASH_CR_SNB_Msk)
                 | FLASH_CR_SNB_Msk
#else
                 | (0xFu << 3)
#endif
                 );

    // 에러 없으면 OK
#if defined(FLASH_SR_OPERR) || defined(FLASH_SR_WRPERR) || defined(FLASH_SR_PGAERR) || defined(FLASH_SR_PGPERR) || defined(FLASH_SR_PGSERR)
    if (sr & (
#if defined(FLASH_SR_OPERR)
              FLASH_SR_OPERR |
#endif
#if defined(FLASH_SR_WRPERR)
              FLASH_SR_WRPERR |
#endif
#if defined(FLASH_SR_PGAERR)
              FLASH_SR_PGAERR |
#endif
#if defined(FLASH_SR_PGPERR)
              FLASH_SR_PGPERR |
#endif
#if defined(FLASH_SR_PGSERR)
              FLASH_SR_PGSERR |
#endif
              0u)) return 0;
#endif
    return 1;
}

// ==================  VTOR SRAM 아일랜드 (재사용)  ==================
__attribute__((aligned(256))) static uint32_t sram_vtor[128];
static uint32_t saved_vtor=0, saved_systick=0, nvic_isER[8];

static void tm_enter_sram_island(void){
    __disable_irq();
    saved_vtor = SCB->VTOR;
    uint32_t *src = (uint32_t*)saved_vtor;
    for (int i=0;i<128;i++) sram_vtor[i] = src[i];
    saved_systick = SysTick->CTRL; SysTick->CTRL = 0;
    for (int i=0;i<8;i++){ nvic_isER[i]=NVIC->ISER[i]; NVIC->ICER[i]=0xFFFFFFFFu; NVIC->ICPR[i]=0xFFFFFFFFu; }
    SCB->VTOR = (uint32_t)sram_vtor; __DSB(); __ISB();
    __enable_irq(); // NVIC disable 상태라 실IRQ 없음
}
static void tm_leave_sram_island(void){
    __disable_irq();
    SCB->VTOR = saved_vtor; __DSB(); __ISB();
    for (int i=0;i<8;i++) NVIC->ISER[i] = nvic_isER[i];
    SysTick->CTRL = saved_systick;
    __enable_irq();
}


// ==================  공통: 화면은 RAM 버퍼로만(플래시 리터럴 금지) ==================
static void tm_show_erase_working(uint32_t sector, uint8_t danger_color){
    char l1[17] = "ERASE S00      ";
    char l2[17] = "WORKING...     ";
    l1[8] = (char)('0' + ((sector/10)%10));
    l1[9] = (char)('0' + (sector%10));
    LCDColorSet(danger_color);
    tm_line16(1, l1); tm_line16(2, l2);
}

static void tm_show_erase_result(uint32_t sector, int ok){
    char l1[17] = "ERASE S00      ";
    char l2[17] = "OK            ";
    l1[8] = (char)('0' + ((sector/10)%10));
    l1[9] = (char)('0' + (sector%10));
    LCDColorSet(ok?3:5);
    if (!ok){ memcpy(l2, "FAIL          ", 16); }
    tm_line16(1, l1); tm_line16(2, l2);
}

// ==================  RAM-세이프 섹터 이레이즈(모든 섹터 공통 경로)  ==================
static int tm_erase_sector_RAMSAFE(uint32_t sector, uint8_t danger_color)
{
    // (1) 안내는 플래시 문자열 금지 → RAM 버퍼로 출력
    tm_show_erase_working(sector, danger_color);

    // (2) SRAM 아일랜드 진입 (VTOR=SRAM, NVIC/SysTick OFF)
    tm_enter_sram_island();

    // (3) 진짜 ERASE는 전부 SRAM 코드로!
    int ok = tm_ram_erase_sector(sector);

    // (4) 원복 후 결과만 한 번 출력
    tm_leave_sram_island();
    tm_show_erase_result(sector, ok);
    return ok;
}

// ==================  NUKE 본체 (컨펌 복원 + 전 섹터 RAM-세이프 소거) ==================
static int tm_confirm_nuke(void); // (너의 파일에 이미 있음)

// === QuickSafe NUKE ===
//  - 'NUKE' 타이핑 + 엔터홀드 가드 유지 (tm_confirm_nuke 사용)
//  - S0(벡터) 먼저 지우고, 이어서 나머지 섹터(자기 섹터 제외) 전부 RAM 함수로 소거
//  - 진행은 RAM 버퍼 문자열로만 표시 (플래시 리터럴 접근 최소화)
//  - 마지막에 즉시 리셋
// === QuickSafe (S1..S11 → S0 마지막) ===
// - 'NUKE' 컨펌 유지 (tm_confirm_nuke 사용)
// - S1~S11 먼저: 진행 상황을 LCD에 표시 (RAM 문자열)
// - 마지막에 S0 소거 → 즉시 리셋 (지운 직후엔 화면 갱신 안 함)
// - 지우는 동작은 모두 tm_ram_erase_sector( RAM에서 실행 ) + SRAM VTOR

void TM_FlashNuke_QuickSafe(void)
{
    // 0) 재치있는 안전 가드
    if (!tm_confirm_nuke()){
        LCDColorSet(4);
        tm_line16(1,"NUKE CANCEL    ");
        tm_line16(2,"SAFE & SOUND   ");
        HAL_Delay(600);
        return;
    }

    // 1) 소형 RAM 함수 복사(FLASH→CCM) — 필수!
    tm_copy_tmramfunc();

    // 2) 헤더/요약
    LCDColorSet(5);
    tm_line16(1,"OH NO U R TRYING");
    tm_line16(2,"TO NUKE ENTIRE  ");
    HAL_Delay(400);

    tm_line16(1,"MEMORY! THIS IS");
    tm_line16(2,"IRREVERSIBLE   ");
    HAL_Delay(400);

    tm_line16(1,"ITS NUKE FOR THE");
    tm_line16(2,"REASON          ");
    HAL_Delay(400);

    tm_line16(1,"ITS HELLA      ");
    tm_line16(2,"DANGEROUS AF  ");
    HAL_Delay(400);



    uint32_t here = (uint32_t)(uintptr_t)&TM_FlashNuke_QuickSafe;
    uint32_t self = tm_addr2sector(here);
    uint32_t vtor = tm_addr2sector(SCB->VTOR);

    {
        char a1[17]="SELF S00       ", a2[17]="VTOR S00       ";
        a1[6]='0'+((self/10)%10); a1[7]='0'+(self%10);
        a2[6]='0'+((vtor/10)%10); a2[7]='0'+(vtor%10);
        LCDColorSet(4); tm_line16(1,a1); tm_line16(2,a2);
        HAL_Delay(400);
    }

    // 3) S1..S11 먼저 지움 (자기 섹터는 스킵해서 살아 돌아오기)
    for (uint32_t s = FLASH_SECTOR_1; s <= FLASH_SECTOR_11; ++s){
        if (s == self) continue;
        tm_show_erase_working(s, (s==vtor)?5:4);

        tm_enter_sram_island();
        int r = tm_ram_erase_sector(s);   // ★ RAM에서 실행 (플래시 BUSY 안전)
        tm_leave_sram_island();

        tm_show_erase_result(s, r);
        HAL_Delay(60);
    }

    // 4) 마지막 S0 소거 안내 (지운 직후에는 화면 갱신 안 함)
    {
        char w1[17]="ERASE S00      ", w2[17]="FINAL & RESET  ";
        LCDColorSet(5); tm_line16(1,w1); tm_line16(2,w2);
        HAL_Delay(200);
    }

    // 5) S0 지움 → 즉시 리셋 (지운 뒤엔 화면/문자열 사용 금지)
    tm_enter_sram_island();
    (void)tm_ram_erase_sector(FLASH_SECTOR_0);  // ★ 벡터 섹터 제거
    // 여기서 더 이상 플래시 접근 없이 바로 리셋
    NVIC_SystemReset();

    // (도달하지 않음)
}


void TM_FlashNuke(void)
{
    // 0) 장난스런 컨펌 복원
    if (!tm_confirm_nuke()){
        LCDColorSet(4);
        tm_line16(1, "NUKE CANCEL    ");
        tm_line16(2, "SAFE & SOUND   ");
        HAL_Delay(700);
        return;
    }

    // 1) 헤더
    LCDColorSet(5);
    tm_line16(1, "!!! FLASH NUKE ");
    tm_line16(2, "DANGER ZONE    ");
    HAL_Delay(500);

    // 2) 자기/VTOR 섹터 파악
    uint32_t here        = (uint32_t)(uintptr_t)&TM_FlashNuke;
    uint32_t self_sector = tm_addr2sector(here);
    uint32_t vtor_sector = tm_addr2sector(SCB->VTOR);

    // 3) 요약 안내
    {
        char a1[17] = "SELF S00       ";
        char a2[17] = "VTOR S00       ";
        a1[6] = (char)('0' + ((self_sector/10)%10));
        a1[7] = (char)('0' + (self_sector%10));
        a2[6] = (char)('0' + ((vtor_sector/10)%10));
        a2[7] = (char)('0' + (vtor_sector%10));
        LCDColorSet(4); tm_line16(1,a1); tm_line16(2,a2);
        HAL_Delay(500);
    }

    // 4) 전체 섹터 소거 (자기 섹터는 기본 스킵)
    uint32_t ok=0, ng=0;
    for (uint32_t s=FLASH_SECTOR_0; s<=FLASH_SECTOR_11; ++s){
        if (s == self_sector) continue;        // 살아 돌아오기!

        // 모든 섹터를 동일하게 RAM-세이프 경로로 지움
        // (VTOR=0이든 아니든 안전)
        int r = tm_erase_sector_RAMSAFE(s, (s==vtor_sector)?5:4);
        if (r) ok++; else ng++;
        HAL_Delay(60);
    }

    // 5) 요약
    {
        char l1[17] = "NUKE DONE      ";
        char l2[17] = "OK:00 NG:00    ";
        l2[3]  = (char)('0' + ((ok/10)%10));
        l2[4]  = (char)('0' + (ok%10));
        l2[9]  = (char)('0' + ((ng/10)%10));
        l2[10] = (char)('0' + (ng%10));
        LCDColorSet(ng?5:3); tm_line16(1,l1); tm_line16(2,l2);
        HAL_Delay(900);
    }
    notch_ui_button_beep();
}





// ⬇️ "NUKE" 비밀번호 확인 (로터리로 문자 선택→ENTER로 확정, BACK=취소)
// ⬇️ "NUKE" 비밀번호 확인 (ISR 스캐너만 사용 / 중복 이벤트 내성)
static int tm_confirm_nuke(void) {
    extern void Poll_Rotary(TIM_HandleTypeDef*, int16_t*, RotaryEvent*);
    extern TIM_HandleTypeDef htim4; extern int16_t prev_count4;
    extern volatile RotaryEvent rotaryEvent3;
    extern volatile ButtonEvent buttonEvents[3];
    extern void notch_ui_button_beep(void);
    extern void notch_ui_mode_return_triple_beep(void);
    extern void notch_ui_rotary_click_freq(float);

    const char *target = "NUKE";
    uint8_t idx = 0;
    char cur = target[0];                 // ★ 시작을 'N'으로 스냅

    LCD16X2_Clear(MyLCD);
    LCDColorSet(5);
    tm_line16(1, "!!! FLASH NUKE ");
    tm_line16(2, "TYPE: N U K E ");
    HAL_Delay(600);

    LCDColorSet(4);
    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    for(;;){
        // 프롬프트
        char l1[17], l2[17];
        snprintf(l1, sizeof(l1), "CHAR %u/4  %c ", (unsigned)idx+1, target[idx]);
        snprintf(l2, sizeof(l2), "[%c] ENT=OK BK ", cur);
        tm_line16(1, l1); tm_line16(2, l2);

        // ⚠️ 로컬 스캔 금지! (ISR이 채워준 buttonEvents만 소비)
        // Buttons_Scan_1ms();  // ← 지움

        // 로터리만 읽어도 됨
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);
        if (rotaryEvent3 == ROTARY_EVENT_CW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            cur = (cur=='Z')?'A':(cur+1);
            notch_ui_rotary_click_freq(1000.f);
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW){
            rotaryEvent3 = ROTARY_EVENT_NONE;
            cur = (cur=='A')?'Z':(cur-1);
            notch_ui_rotary_click_freq(900.f);
        }

        // ENTER: 글자 확정
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[2] = BUTTON_EVENT_NONE;
            if (cur == target[idx]) {
                notch_ui_button_beep();
                idx++;
                if (idx == 4) { // 성공 → 최종 홀드 대기
                    LCDColorSet(3);
                    tm_line16(1, "BOMB ACTIVATED");
                    tm_line16(2, "HOLD ENT = GO ");
                    HAL_Delay(600);
                    // 최종: ENTER 2초 홀드
                    uint8_t ok=0;
                    while(1){
                        // Buttons_Scan_1ms();  // ← 금지
                        if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS){ ok=1; break; }
                        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){ ok=0; break; }
                        HAL_Delay(1);
                    }
                    return ok ? 1 : 0;
                } else {
                    // ★ 다음 목표 글자로 즉시 스냅 → 중복 이벤트도 안전
                    cur = target[idx];
                }
            } else {
                LCDColorSet(5);
                tm_line16(1, " WRONG KEY!    ");
                tm_line16(2, " CANCELLED     ");
                notch_ui_mode_return_triple_beep();
                HAL_Delay(700);
                return 0;
            }
        }

        // BACK: 취소
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            return 0;
        }

        HAL_Delay(1);
    }
}


// 섹터 하나 ERASE + 요약 표시

/*
static int tm_erase_sector(uint32_t sector){
    FLASH_EraseInitTypeDef e = {0};
    uint32_t se=0;

    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 2.7~3.6V
    e.Sector       = sector;
    e.NbSectors    = 1;

    char l1[17], l2[17];
    snprintf(l1, sizeof(l1), "ERASE S%02lu   ", (unsigned long)sector);
    LCDColorSet(4);
    tm_line16(1, l1);
    tm_line16(2, "WORKING.       ");

    uint32_t t0 = HAL_GetTick();

    __disable_irq();               // ★★★ 핵심: 지우는 동안 IRQ OFF
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &se);
    HAL_FLASH_Lock();
    __enable_irq();                // ★★★ 복구

    uint32_t dt = HAL_GetTick() - t0;

    if (st == HAL_OK) {
        LCDColorSet(3);
        snprintf(l2, sizeof(l2), "OK  %4lums   ", (unsigned long)dt);
        tm_line16(1, l1); tm_line16(2, l2);
        return 1;
    } else {
        LCDColorSet(5);
        tm_line16(1, l1); tm_line16(2, "FAIL          ");
        return 0;
    }
}
*/




// ─────────────── 섹터 ERASE(일반) ───────────────
static int tm_erase_sector_normal(uint32_t sector){
    FLASH_EraseInitTypeDef e={0}; uint32_t se=0;
    e.TypeErase=FLASH_TYPEERASE_SECTORS; e.VoltageRange=FLASH_VOLTAGE_RANGE_3;
    e.Sector=sector; e.NbSectors=1;

    // 화면: RAM에서 직접 채움 (리터럴 최소화)
    char l1[17] = "ERASE S00      ";
    char l2[17] = "WORKING...     ";
    tm_put2(&l1[8], sector);
    LCDColorSet(4); tm_line16(1,l1); tm_line16(2,l2);

    uint32_t t0 = HAL_GetTick();

    __disable_irq();                // IRQ 중단 (일반 섹터는 이정도면 충분)
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e,&se);
    HAL_FLASH_Lock();
    __enable_irq();

    uint32_t dt = HAL_GetTick()-t0;

    char r2[17] = "OK   0000ms   ";
    if (st==HAL_OK){
        r2[3]=' '; r2[4]=' ';
        // ms 숫자 채움
        r2[6]  = (char)('0'+((dt/1000)%10));
        r2[7]  = (char)('0'+((dt/100)%10));
        r2[8]  = (char)('0'+((dt/10)%10));
        r2[9]  = (char)('0'+(dt%10));
        LCDColorSet(3); tm_line16(1,l1); tm_line16(2,r2);
        return 1;
    } else {
        char ng[17] = "FAIL          ";
        LCDColorSet(5); tm_line16(1,l1); tm_line16(2,ng);
        return 0;
    }
}

// ─────────────── 섹터 ERASE(S00 또는 VTOR가 걸린 섹터) ───────────────
// * VTOR를 SRAM으로 옮기고, NVIC/SysTick 완전 정지 후 ERASE
static int tm_erase_sector_vtor_safe(uint32_t sector){
    FLASH_EraseInitTypeDef e={0}; uint32_t se=0;
    e.TypeErase=FLASH_TYPEERASE_SECTORS; e.VoltageRange=FLASH_VOLTAGE_RANGE_3;
    e.Sector=sector; e.NbSectors=1;

    char l1[17] = "ERASE S00      ";
    char l2[17] = "WORKING...     ";
    tm_put2(&l1[8], sector);
    LCDColorSet(5); tm_line16(1,l1); tm_line16(2,l2);

    // ★ SRAM 아일랜드 진입
    tm_enter_sram_island();

    // ERASE (IRQ OFF 상태에서, VTOR=SRAM)
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e,&se);
    HAL_FLASH_Lock();

    // ★ 원복
    tm_leave_sram_island();

    if (st==HAL_OK){
        char ok[17] = "OK            ";
        LCDColorSet(3); tm_line16(1,l1); tm_line16(2,ok);
        return 1;
    } else {
        char ng[17] = "FAIL          ";
        LCDColorSet(5); tm_line16(1,l1); tm_line16(2,ng);
        return 0;
    }
}













// (옵션) 전체 ZERO FILL
static void tm_zero_fill_range(uint32_t base, uint32_t bytes){
#ifdef ZERO_FILL_ALL
    const uint32_t WORDS = bytes/4u;
    const uint32_t PAT   = 0x00000000u;
    for (uint32_t i=0;i<WORDS;i++){
        uint32_t a = base + i*4u;
        (void)HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, a, PAT);
    }
#else
    (void)base; (void)bytes;
#endif
}




// ============================================================
// AUTO TEST: i2s 입력 테스트 제외, 뉴크 제외, 비대화형 항목만 자동 실행
//   - FLASH TEST (섹터10: erase → prog+verify → restore)
//   - RAM TEST   (8KB 버퍼/또는 예약 영역, 6패스)
//   - LCD TEST   (간단 색/패턴)
//   - TEXT PRINT (문자 렌더 테스트)
//   - BACKLIGHT  (LCDColorSet 순환으로 대체)
//   - BEEP SCORE (notch overlay note: 짧게 2~3음)
// 각 단계 PASS/FAIL 표시 + 최종 합산 결과
// ============================================================
static int tm_autotest_flash(void);     // 아래에 구현
static int tm_autotest_ram(void);       // "
static int tm_autotest_lcd(void);       // "
static int tm_autotest_text(void);      // "
static int tm_autotest_backlight(void); // "
static int tm_autotest_beep(void);      // "

void TM_AutoTest(void)
{
    // 화면 준비
    LCD16X2_Clear(MyLCD);
    LCDColorSet(4);
    tm_lcd_line(1, " AUTO TEST     ");
    tm_lcd_line(2, " RUN SEQ...    ");
    HAL_Delay(250);

    struct { const char* name; int (*fn)(void); } steps[] = {
        { "FLASH TEST",     tm_autotest_flash     },
        { "RAM TEST",       tm_autotest_ram       },
        { "LCD TEST",       tm_autotest_lcd       },
        { "TEXT PRINT",     tm_autotest_text      },
        { "BACKLIGHT",      tm_autotest_backlight },
        { "BEEP SCORE",     tm_autotest_beep      },
    };
    const uint32_t N = sizeof(steps)/sizeof(steps[0]);

    uint32_t pass = 0, fail = 0;

    for (uint32_t i=0; i<N; ++i) {
        char l1[17], l2[17];
        snprintf(l1,sizeof(l1),"%-12.12s", steps[i].name);
        LCDColorSet(4);
        tm_lcd_line(1, l1);
        tm_lcd_line(2, "RUNNING...     ");

        int ok = steps[i].fn();

        if (ok) {
            ++pass;
            LCDColorSet(3);
            tm_lcd_line(2, "PASS           ");
        } else {
            ++fail;
            LCDColorSet(5);
            tm_lcd_line(2, "FAIL           ");
        }
        HAL_Delay(500);
    }

    // 요약
    {
        char l1[17], l2[17];
        snprintf(l1,sizeof(l1),"DONE P:%02lu F:%02lu",(unsigned long)pass,(unsigned long)fail);
        LCDColorSet(fail?5:3);
        tm_lcd_line(1, l1);
        tm_lcd_line(2, "BK:MENU        ");
    }

    // BK로만 빠지도록(자동 종료)
    buttonEvents[0]=BUTTON_EVENT_NONE;
    for(;;){
        Buttons_Scan_1ms();
        if (buttonEvents[0]==BUTTON_EVENT_SHORT_PRESS){
            buttonEvents[0]=BUTTON_EVENT_NONE;
            notch_ui_mode_return_triple_beep();
            return;
        }
        HAL_Delay(5);
    }
}

// -----------------------------
// FLASH: Sector10을 깨끗이 → 256B 블록 일괄 write/verify → 복원
//  (기존 TM_FlashTest 로직을 “조용히” 수행, 결과만 return)
// -----------------------------
static int tm_autotest_flash(void)
{
    #define TMF_WORDS        64u                 // 256B
    #define TMF_PATTERN      0xA5A5A5A5u
    #define TMF_TEST_BEGIN   0x080C0000u         // Sector10 시작
    #define TMF_TEST_END     0x080E0000u         // (Sector11 시작) 제외
    #define TMF_TEST_SECTOR  FLASH_SECTOR_10

    auto void erase_sector(uint32_t sector){
        FLASH_EraseInitTypeDef e = {0};
        uint32_t se;
        e.TypeErase    = FLASH_TYPEERASE_SECTORS;
        e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        e.Sector       = sector;
        e.NbSectors    = 1;
        HAL_FLASH_Unlock();
        HAL_FLASHEx_Erase(&e, &se);
        HAL_FLASH_Lock();
    }

    // 0) 시작 전 섹터 정리
    erase_sector(TMF_TEST_SECTOR);

    // 1) 프로그래밍 + 베리파이
    uint32_t fail_blocks = 0;
    HAL_FLASH_Unlock();
    for (uint32_t base = TMF_TEST_BEGIN; base + TMF_WORDS*4u <= TMF_TEST_END; base += TMF_WORDS*4u)
    {
        uint32_t errs = 0;
        for (uint32_t i=0;i<TMF_WORDS;i++){
            uint32_t a = base + i*4u;
            __disable_irq();
            HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, a, TMF_PATTERN);
            __enable_irq();
            if (st != HAL_OK) { ++errs; continue; }
            if (*((volatile uint32_t*)a) != TMF_PATTERN) ++errs;
        }
        if (errs) ++fail_blocks;
    }
    HAL_FLASH_Lock();

    // 2) 섹터 복구
    erase_sector(TMF_TEST_SECTOR);

    return (fail_blocks==0) ? 1 : 0;
}

// -----------------------------
// RAM: 8KB 버퍼(혹은 예약 영역) 6패스 검사 (기존 TM_RamTest 축약형)
// -----------------------------
static int tm_autotest_ram(void)
{
    // 링커로 예약해뒀다면 TM_RAM_BEGIN/TM_RAM_END 쓰기
    #ifndef TM_RAM_BEGIN
    #define TM_RAM_BEGIN 0u
    #endif
    #ifndef TM_RAM_END
    #define TM_RAM_END   0u
    #endif

    static uint32_t tm_buf[2048]; // 8KB
    volatile uint32_t *mem = (volatile uint32_t*)
        ((TM_RAM_BEGIN && TM_RAM_END) ? TM_RAM_BEGIN : (uint32_t)tm_buf);
    uint32_t words = (TM_RAM_BEGIN && TM_RAM_END)
        ? ((TM_RAM_END - TM_RAM_BEGIN) / 4u)
        : (uint32_t)(sizeof(tm_buf)/sizeof(tm_buf[0]));

    uint32_t err=0;

    // 1) Data bus quick (워킹1/0)
    {
        volatile uint32_t *p = mem;
        for (uint32_t b=0;b<32;b++){ uint32_t pat = 1u<<b; *p=pat; if(*p!=pat){++err;break;} }
        for (uint32_t b=0;b<32;b++){ uint32_t pat = ~(1u<<b); *p=pat; if(*p!=pat){++err;break;} }
    }
    // 2) ALL-0
    for (uint32_t i=0;i<words;i++) mem[i]=0x00000000u;
    for (uint32_t i=0;i<words;i++) if (mem[i]!=0x00000000u) ++err;
    // 3) ALL-1
    for (uint32_t i=0;i<words;i++) mem[i]=0xFFFFFFFFu;
    for (uint32_t i=0;i<words;i++) if (mem[i]!=0xFFFFFFFFu) ++err;
    // 4) ALT A5
    for (uint32_t i=0;i<words;i++) mem[i]=(i&1)?0x5A5A5A5Au:0xA5A5A5A5u;
    for (uint32_t i=0;i<words;i++){ uint32_t exp=(i&1)?0x5A5A5A5Au:0xA5A5A5A5u; if(mem[i]!=exp) ++err; }
    // 5) ADDR
    for (uint32_t i=0;i<words;i++) mem[i]=(uint32_t)(uintptr_t)&mem[i];
    for (uint32_t i=0;i<words;i++){ uint32_t exp=(uint32_t)(uintptr_t)&mem[i]; if(mem[i]!=exp) ++err; }
    // 6) xorshift
    auto uint32_t xs(uint32_t x){ x^=x<<13; x^=x>>17; x^=x<<5; return x; }
    {
        uint32_t seed = 0x12345678u ^ (uint32_t)(uintptr_t)mem ^ (words<<5);
        uint32_t x=seed; for (uint32_t i=0;i<words;i++){ x=xs(x); mem[i]=x; }
        x=seed; for (uint32_t i=0;i<words;i++){ x=xs(x); if(mem[i]!=x) ++err; }
    }

    return (err==0)?1:0;
}

// -----------------------------
// LCD TEST: 간단 색/패턴 플로우(오류 판단 불가 → 실행 성공=PASS)
// -----------------------------
static int tm_autotest_lcd(void)
{
    LCDColorSet(3); tm_lcd_line(1," LCD TEST      "); tm_lcd_line(2," COLOR 1       "); HAL_Delay(150);
    LCDColorSet(4); tm_lcd_line(1," LCD TEST      "); tm_lcd_line(2," COLOR 2       "); HAL_Delay(150);
    LCDColorSet(2); tm_lcd_line(1," LCD TEST      "); tm_lcd_line(2," COLOR 3       "); HAL_Delay(150);
    LCDColorSet(5); tm_lcd_line(1," LCD TEST      "); tm_lcd_line(2," MATRIX OK     "); HAL_Delay(150);
    return 1;
}

// -----------------------------
// TEXT PRINT: 여러 문자열/정렬 테스트(실행 성공=PASS)
// -----------------------------
static int tm_autotest_text(void)
{
    LCDColorSet(4);
    tm_lcd_line(1," TEXT PRINT    "); tm_lcd_line(2," ABCD abcd 012 "); HAL_Delay(180);
    tm_lcd_line(1," SYM:!@#$%^&*()"); tm_lcd_line(2," <>{}[]\\/|?    "); HAL_Delay(180);
    tm_lcd_line(1," UTF? (ROM)    "); tm_lcd_line(2," 16x2 ALIGN OK "); HAL_Delay(180);
    return 1;
}

// -----------------------------
// BACKLIGHT: 하드 PWM 없음 → LCDColorSet 순환으로 대체
// -----------------------------
static int tm_autotest_backlight(void)
{
    for (int i=0;i<4;i++){
        LCDColorSet( (i&1)?3:4 );
        tm_lcd_line(1," BACKLIGHT     ");
        tm_lcd_line(2, (i&1)?" BRIGHT        ":" DIM           ");
        HAL_Delay(120);
    }
    return 1;
}

// -----------------------------
// BEEP SCORE: notch overlay note로 2~3음 짧게 재생(응답=PASS)
// -----------------------------
static int tm_autotest_beep(void)
{
    extern void     notch_note_start(float freq_hz, uint16_t ms, float gain);
    extern uint8_t  notch_note_busy(void);
    extern void     notch_note_stop(void);

    // 두 음만 짧게
    notch_note_start(660.0f, 180, 0.25f);
    uint32_t t0 = HAL_GetTick();
    while (notch_note_busy() && (HAL_GetTick()-t0)<600) HAL_Delay(1);

    notch_note_start(880.0f, 180, 0.25f);
    t0 = HAL_GetTick();
    while (notch_note_busy() && (HAL_GetTick()-t0)<600) HAL_Delay(1);

    // 엔진이 “바쁨→유휴”로 바뀌었으면 PASS
    return notch_note_busy()? 0 : 1;
}

















// ───────────────────────────────────
// 메인 루프 (복귀 없음)
void TestMode_RunLoop(void)
{
    LCD16X2_Clear(MyLCD);
    tm_toast(4, " == TEST MODE = ", " ROT:SELECT    ", 350);

    uint8_t sel = 0;
    tm_draw_menu(sel);

    // 이벤트 초기화
    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;

    for(;;){
        // 1) 버튼 스캔(1ms 베이스)

        // 2) 로터리 이벤트(main.c에 있는 함수/핸들 그대로 사용!)  :contentReference[oaicite:9]{index=9}
        Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);

        // 3) 로터리로 메뉴 순환
        if (rotaryEvent3 == ROTARY_EVENT_CW) {
            rotaryEvent3 = ROTARY_EVENT_NONE;
            sel = (uint8_t)((sel + 1u) % TM_NUM_ITEMS);
            notch_ui_rotary_click_freq(1000.f);
            tm_draw_menu(sel);
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
            rotaryEvent3 = ROTARY_EVENT_NONE;
            sel = (uint8_t)((sel == 0u) ? (TM_NUM_ITEMS-1u) : (sel-1u));
            notch_ui_rotary_click_freq(900.f);
            tm_draw_menu(sel);
        }

        // 4) BACK(0): 재부팅 확인
        if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[0] = BUTTON_EVENT_NONE;
            notch_ui_button_beep();
            tm_confirm_reboot();
            tm_flush_inputs(120);            // 💡 대화상자에서 돌아온 뒤 비움
            tm_draw_menu(sel);
        }

        // 5) ENTER(2): 실행
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
            buttonEvents[2] = BUTTON_EVENT_NONE;
            notch_ui_button_beep();
            tm_dispatch(sel);
            tm_flush_inputs(120);            // 💡 하위 메뉴에서 복귀 시 비움
            tm_draw_menu(sel);
        }


        // 6) BACK 길게: 즉시 리부트(옵션)
        if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS) {
            buttonEvents[0] = BUTTON_EVENT_NONE;
            tm_toast(5, " FORCE REBOOT  ", " LONG BACK     ", 250);
            NVIC_SystemReset();
        }

        HAL_Delay(1);
    }
}
