/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *\
  *\
  *\
  *\for 20004
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#define ARM_MATH_CM4
#include <stdio.h>
#include "Util.h"
#include "LCD16X2.h"
#include "LCD16X2_cfg.h"
#include "notch.h"

#include "PowerFlash.h"   // ★ 추가
#include "PowerSave.h"   // ★ 추가
#include "TESTMODE.h"   // ★ 추가
#include "BatteryTemp.h"
#include "LEDcontrol.h"
#include "Clock.h"


#include <math.h>
#include <stdlib.h> // abs()
#include <arm_math.h>




/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/////////////////////////////////
//============DSP================
void notch_init(uint32_t fs_hz);
HAL_StatusTypeDef notch_start(void);
void notch_task(void);
void notch_stop(void);

//=============================



typedef enum {
    // HELLO USER!
	UI_WELCOME_SCREEN,

	// MODE SELECTOR
    UI_MODE_SELECTION,

    // Practice Group
	UI_PRACTIVE_MODE_INTRO,
    UI_PRACTICE_HOME,
    UI_PRACTICE_FREQ_SETTING_CUTOFF_START,
	UI_PRACTICE_FREQ_SETTING_CUTOFF_END,
    UI_PRACTICE_INST_PRESET_SETTING,

    // Tuner
	UI_TUNER_MODE_INTRO,
    UI_TUNER_HOME,
    UI_TUNER_BASE_A_FREQ_SETTING,


    // Metronome
	UI_METRONOME_MODE_INTRO,
    UI_METRONOME_HOME,
	UI_METRONOME_BPM_SETTING,
	UI_METRONOME_TIME_SIGNATURE_SETTING,
	UI_METRONOME_TIMING_CALCULATION,

    // Sound Generator
	UI_SOUNDGEN_INTRO,
    UI_SOUNDGEN_HOME,
	UI_SOUNDGEN_BASE_A_FREQ_SETTING,

    // Settings
    UI_SETTINGS_HOME,
	UI_SETTINGS_METRONOME_LENGTH,
	UI_SETTINGS_POWEROFF_CONFIRM,
	UI_SETTINGS_ABOUT,
	UI_SETTINGS_FW_UPDATE,

	// VOLUME AND BAL
	UI_MASTER_VOLUME,
	UI_SOUND_BALANCE,
	UI_METRONOME_VOLUME,


    UI_STATE_COUNT
} UIState;

UIState currentUIState = UI_WELCOME_SCREEN;





typedef enum {
	DSP_Standby,
	DSP_AudioLoopBack,
	DSP_Tuner,
	DSP_Metronome,
	DSP_SoundGenerator,
	DSP_TestMode1,
	DSP_TestMode2,

    DSP_MODE_STATE_COUNT
} DSPModeState;

DSPModeState currentDSPModeState = DSP_Standby;
///==========================


//=================FOR ROTARY ENC=======================
typedef enum {
    ROTARY_EVENT_NONE = 0,
    ROTARY_EVENT_CW,
    ROTARY_EVENT_CCW
} RotaryEvent;


char buf[17];

int16_t prev_count2 = 0;
int16_t prev_count3 = 0;
int16_t prev_count4 = 0;

int32_t rotaryVal2 = 0;
int32_t rotaryVal3 = 0;
int32_t rotaryVal4 = 0;


int16_t prev_count = 0;

int32_t testnumber;

volatile RotaryEvent rotaryEvent1 = ROTARY_EVENT_NONE;
volatile RotaryEvent rotaryEvent2 = ROTARY_EVENT_NONE;
volatile RotaryEvent rotaryEvent3 = ROTARY_EVENT_NONE;
//=============================


//=============FOR BUTTON INPUT=================
#define DEBOUNCE_DELAY 250  // ms

#define LONG_PRESS_MS   1000

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint8_t prevState;
    uint32_t lastTick;
} ButtonState;

ButtonState buttons[3] = {
    {GPIOE, GPIO_PIN_2, 1, 0},  // PC0
    {GPIOE, GPIO_PIN_3, 1, 0},  // PC1
    {GPIOE, GPIO_PIN_4, 1, 0}   // PC2
};


typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS
} ButtonEvent;

volatile ButtonEvent buttonEvents[3] = {BUTTON_EVENT_NONE, BUTTON_EVENT_NONE, BUTTON_EVENT_NONE};


volatile uint8_t modeChangedFlag = 0;
volatile uint8_t modeDisplayTimer = 0;

volatile uint32_t last_btn_tick[3] = {0, 0, 0};

// 버튼 눌림 지속 시간 측정
volatile uint32_t btn2_press_tick = 0;
volatile uint8_t btn2_pressed = 0;
#define LONG_PRESS_THRESHOLD 1000  // 1초 이상이면 길게 누름
///===================================

////==========애니메이션 관련========================
static volatile uint8_t g_mode_anim_busy = 0;  // ★ 애니메이션 중 입력 잠금
///===================================



// 마이크 무음 저장
static uint16_t g_prevSoundBalance = 0;
static uint8_t  g_micMuted = 0; // 0=정상, 1=음소거(0 적용중)











/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MyLCD LCD16X2_1

static const uint8_t heartChar[8] = {
    0b00000,
    0b01010,
    0b11111,
    0b11111,
    0b11111,
    0b01110,
    0b00100,
    0b00000
};


static const uint8_t MelodyIcon[8] = {
	0b00001,
	0b00011,
	0b00101,
	0b01001,
	0b01001,
	0b01011,
	0b11011,
	0b11000
};

static const uint8_t Paused[8] = {
	0b00000,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b00000
};

static const uint8_t Playing[8] = {
	0b00000,
	0b01100,
	0b01110,
	0b01111,
	0b01111,
	0b01110,
	0b01100,
	0b00000
};

static const uint8_t Loopback[8] = {
	0b01000,
	0b11111,
	0b01001,
	0b00001,
	0b10000,
	0b10010,
	0b11111,
	0b00010
};

static const uint8_t TuningFork[8] = {
		  0x0A,
		  0x0A,
		  0x0A,
		  0x0E,
		  0x0E,
		  0x04,
		  0x04,
		  0x04
};



static const uint8_t Timer[8] = {
	0b11111,
	0b11111,
	0b01110,
	0b00100,
	0b00100,
	0b01110,
	0b11111,
	0b11111
};



static const uint8_t MenuFirst[] = {
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00100,
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00000
};

static const uint8_t MenuSecond[] = {
	  0B00000,
	  0B01000,
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00010,
	  0B00000,
	  0B00000
};

static const uint8_t MenuThird[] = {
	  0B00000,
	  0B01000,
	  0B00000,
	  0B00100,
	  0B00000,
	  0B00010,
	  0B00000,
	  0B00000
};

static const uint8_t MenuFourth[] = {
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00000,
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00000
};

static const uint8_t MenuFifth[] = {
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00100,
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00000
};

static const uint8_t MetronomeNow[] = {
	  0B00000,
	  0B00000,
	  0B01110,
	  0B01110,
	  0B01110,
	  0B01110,
	  0B00000,
	  0B00000
};

static const uint8_t MetronomePriv[] = {
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00100,
	  0B00100,
	  0B00000,
	  0B00000,
	  0B00000
};


static const uint8_t SineShape1[] = {
		  0x0E,
		  0x11,
		  0x11,
		  0x11,
		  0x01,
		  0x01,
		  0x01,
		  0x00
};

static const uint8_t SineShape2[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x00,
		  0x02,
		  0x02,
		  0x02,
		  0x1C
};

static const uint8_t SquareShape1[] = {
		  0x1F,
		  0x11,
		  0x11,
		  0x11,
		  0x01,
		  0x01,
		  0x01,
		  0x01
};



static const uint8_t SquareShape2[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x00,
		  0x02,
		  0x02,
		  0x02,
		  0x1E
};


static const uint8_t TriangleShape1[] = {
		  0x00,
		  0x04,
		  0x0A,
		  0x11,
		  0x00,
		  0x00,
		  0x00,
		  0x00
};


static const uint8_t TriangleShape2[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x00,
		  0x11,
		  0x0A,
		  0x04,
		  0x00
};

static const uint8_t PitchShiftCheck[] = {
		  0x0E,
		  0x04,
		  0x00,
		  0x1F,
		  0x1F,
		  0x00,
		  0x04,
		  0x0E
};

static const uint8_t PitchShiftBar[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x1F,
		  0x1F,
		  0x00,
		  0x00,
		  0x00
};

static const uint8_t flaticon[] = {
		  0x10,
		  0x10,
		  0x14,
		  0x1A,
		  0x13,
		  0x14,
		  0x08,
		  0x00
};

static const uint8_t VOLUMEONE[] = {
		0x0,0x0,0x0,0x0,0x0,0x0,0x1f
};


static const uint8_t VOLUMETWO[] = {
		0x0,0x0,0x0,0x0,0x0,0x1f,0x1f
};


static const uint8_t VOLUMETHREE[] = {
		0x0,0x0,0x0,0x0,0x1f,0x1f,0x1f
};


static const uint8_t VOLUMEFOUR[] = {
		0x0,0x0,0x0,0x1f,0x1f,0x1f,0x1f
};


static const uint8_t VOLUMEFIVE[] = {
		0x0,0x0,0x1f,0x1f,0x1f,0x1f,0x1f
};

static const uint8_t VOLUMESIX[] = {
		0x0,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f
};

static const uint8_t VOLUMESEVEN[] = {
		0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f
};

static const uint8_t OneFilledVU[] = {
  0x00,
  0x00,
  0x10,
  0x10,
  0x10,
  0x10,
  0x00,
  0x00
};

static const uint8_t HalfFilledVU[] = {
  0x00,
  0x00,
  0x18,
  0x18,
  0x18,
  0x18,
  0x00,
  0x00
};

static const uint8_t ThreeFilledVU[] = {
		  0x00,
		  0x00,
		  0x1A,
		  0x1A,
		  0x1A,
		  0x1A,
		  0x00,
		  0x00
};

static const uint8_t FullFilledVU[] = {
  0x00,
  0x00,
  0x1B,
  0x1B,
  0x1B,
  0x1B,
  0x00,
  0x00
};






static const uint8_t Rmini[] = {
		  0x00,
		  0x1E,
		  0x12,
		  0x1E,
		  0x18,
		  0x14,
		  0x12,
		  0x00
};

static const uint8_t Lmini[] = {
		  0x00,
		  0x10,
		  0x10,
		  0x10,
		  0x10,
		  0x10,
		  0x1E,
		  0x00
};

const uint8_t degree[] = {
  0x1C,
  0x14,
  0x1C,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00
};

const uint8_t clock_icon[8] = {
    0b00000,
    0b01110,
    0b10101,
    0b10111,
    0b10001,
    0b01110,
    0b00000,
    0b00000
};







////////////////////

static const char* const NoteNames[] = {
    "A  0", "A# 0", "B  0",
    "C  1", "C# 1", "D  1", "D# 1", "E  1", "F  1", "F# 1", "G  1", "G# 1",
    "A  1", "A# 1", "B  1",
    "C  2", "C# 2", "D  2", "D# 2", "E  2", "F  2", "F# 2", "G  2", "G# 2",
    "A  2", "A# 2", "B  2",
    "C  3", "C# 3", "D  3", "D# 3", "E  3", "F  3", "F# 3", "G  3", "G# 3",
    "A  3", "A# 3", "B  3",
    "C  4", "C# 4", "D  4", "D# 4", "E  4", "F  4", "F# 4", "G  4", "G# 4",
    "A  4", "A# 4", "B  4",
    "C  5", "C# 5", "D  5", "D# 5", "E  5", "F  5", "F# 5", "G  5", "G# 5",
    "A  5", "A# 5", "B  5",
    "C  6", "C# 6", "D  6", "D# 6", "E  6", "F  6", "F# 6", "G  6", "G# 6",
    "A  6", "A# 6", "B  6",
    "C  7", "C# 7", "D  7", "D# 7", "E  7", "F  7", "F# 7", "G  7", "G# 7",
    "A  7"
};

// ===== Instrument catalog (compact, ≤12 chars each) =====
typedef enum {
    Instrument_WIDE_HIGH = 0,
    Instrument_WIDE_MID,
    Instrument_WIDE_LOW,
    Instrument_NARROW_HIGH,
    Instrument_NARROW_MID,
    Instrument_NARROW_LOW,
    Instrument_DRUMSET,
    Instrument_XYLOPHONE,
    Instrument_USER_PRESET_1,
    Instrument_USER_PRESET_2,
    Instrument_USER_PRESET_3,
    Instrument_MAX
} InstrumentType;

InstrumentType CurrentInstrumentType = Instrument_WIDE_MID;

#define IS_USER_PRESET(t)  ( \
    (t) == Instrument_USER_PRESET_1 || \
    (t) == Instrument_USER_PRESET_2 || \
    (t) == Instrument_USER_PRESET_3   \
)


static const char* const InstrumentNameTable[] = {
    "WIDE HIGH",       // 0
    "WIDE MID",        // 1
    "WIDE LOW",        // 2
    "NARROW HIGH",     // 3
    "NARROW MID",      // 4
    "NARROW LOW",      // 5
    "DRUMSET",         // 6
    "XYLOPHONE",       // 7
    "USER PRESET1",    // 8
    "USER PRESET2",    // 9
    "USER PRESET3"     // 10
};



/////////////////////////////////

typedef enum {
	Battery_Alkaline,
	Battery_Nimh,
	Battery_Lithium

} BatteryType;

BatteryType CurrentBatteryType = Battery_Alkaline;


//////////////////////////////

// =====[Practice: DIRECT OUT 타이머/스톱워치 메뉴 - STUB 전역]=====
typedef enum {
    PRACT_TM_ITEM_DIRECT_OUT = 0,   // "DIRECT OUT" (원래 문구)
    PRACT_TM_ITEM_24B48K     = 1,   // "24b / 48k"
    PRACT_TM_ITEM_SW_NOW     = 2,   // 현재 스톱워치
    PRACT_TM_ITEM_SW_TOTAL   = 3,   // TOTAL 스톱워치
    PRACT_TM_ITEM_TIMER_MIN  = 4,   // 0~99 분 설정
    PRACT_TM_ITEM_TIMER_RESET= 5,   // "TIMER RESET?"
    PRACT_TM_ITEM_TIMER_SET  = 6,   // "TIMER SET?"
    PRACT_TM_ITEM_SW_RESET   = 7    // "STOPWATCH RESET?"
} PractTimerMenuItem;

volatile uint8_t  g_pract_menu_index = PRACT_TM_ITEM_DIRECT_OUT; // 회전 메뉴 인덱스
volatile uint8_t  g_pract_menu_dirty = 1;     // 2행(3~14) 텍스트 재계산/재표시 요청

// 스톱워치(ms)
volatile uint32_t g_sw_now_ms   = 0;   // 현재
volatile uint32_t g_sw_total_ms = 0;   // 누계
volatile uint8_t  g_sw_running  = 0;   // PracticeHome에 있을 때 자동 On(STUB)

// 타이머
volatile uint16_t g_timer_set_min      = 0;     // 0..99
volatile int32_t  g_timer_remaining_ms = -1;    // <0: 비활성
volatile uint8_t  g_timer_running      = 0;
volatile uint8_t  g_timer_expired_flag = 0;     // 만료 알림 플래그(STUB)
volatile uint8_t  g_timer_edit_min   = 1;       // 로터리 편집 전용(확정 전까지 여기에만 반영)
static  uint32_t  s_timer_color_restore_ms = 0; // 타임업 후 LCD 색 복귀 시각(0=비활성)

// 2행(열3..14) 부분 갱신 캐시 (12칸)
static char       s_line2_cache[12];
static uint8_t    s_line2_cache_valid = 0;
static uint8_t    s_icon_on_prev      = 0;      // 시계 아이콘(2,3) 표시 캐시

// 실시간 갱신용
static uint32_t   s_last_tick_ms      = 0;

// ===== TRIP(현재 시간) 컨트롤 =====
volatile uint8_t  g_sw_trip_run    = 0;   // 버튼2(Short)로 ON/OFF
volatile uint8_t  g_sw_now_frozen  = 0;   // 999'59" 초과시 1로 고정(리셋 전까지 정지)

// =====[TIMER RUN/PAUSE & FINISH - for Remaining Time(UI: -mm'ss")]=====
volatile uint8_t  g_timer_run      = 0;   // 1: 카운트다운 진행, 0: 일시정지
volatile uint8_t  g_timer_finished = 0;   // 1: 0 도달 후 얼림(리셋 전까지 업데이트 중지)

// ==== [Timer Set / Reset UI STUB Flags] ====
volatile uint8_t g_timer_set_mode = 0;     // 1: "타이머 설정 화면" 활성. UI만 유지(입력 로직 나중)
volatile uint8_t g_timer_reset_phase = 0;  // 0: 기본 "TMR RESET?", 1: "RESET!", 2: "RESET OK"
volatile uint8_t g_sw_reset_phase    = 0;  // 0: 기본 "SW RESET?",  1: "RESET!", 2: "RESET OK"

volatile uint32_t g_sw_session_ms = 0;


/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2S_HandleTypeDef hi2s2;
I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_i2s2_ext_rx;
DMA_HandleTypeDef hdma_spi2_tx;
DMA_HandleTypeDef hdma_spi3_rx;

RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim5;
TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim7;
TIM_HandleTypeDef htim8;
TIM_HandleTypeDef htim9;
TIM_HandleTypeDef htim12;

/* USER CODE BEGIN PV */

uint16_t VolumeTimer = 0;
uint8_t UITotalBlinkStatus = 0;




uint8_t Welcomeeventisdone = 0;

uint8_t CurrentModeStatus = 0;
uint8_t CurrentPracticeGroup = 0;
uint8_t CurrentDisplayStatus = 0;

uint8_t ModeSelectionPrevDisplay       = 255;

uint8_t ModeSelectionFirstRunFlag      = 0;
uint8_t PracticeHomeFirstRunFlag       = 0;
uint8_t TunerHomeFirstRunFlag          = 0;
uint8_t MetronomeHomeFirstRunFlag      = 0;
uint8_t SoundGenHomeFirstRunFlag       = 0;
uint8_t SettingsHomeFirstRunFlag       = 0;
uint8_t VolumeControlFirstRunFlag      = 0;
uint8_t BalanceControlFirstRunFlag     = 0;




//======== 프랙티스 모드 함수 ==============
volatile uint8_t AudioProcessingIsReady = 0;


uint32_t CutOffFreqStart = 300;
uint32_t CutOffFreqEnd = 600;

uint32_t CutOffFreqStartUser1 = 300;
uint32_t CutOffFreqEndUser1   = 3000;

uint32_t CutOffFreqStartUser2 = 80;
uint32_t CutOffFreqEndUser2   = 800;

uint32_t CutOffFreqStartUser3 = 1000;
uint32_t CutOffFreqEndUser3   = 8000;


// 0일시 패스스루, 1일시 컷오프, 2일시 피치쉬프트.
uint8_t CutOffOnOff = 0; // 컷오프뿐만 아니라 모든 기능을 정합니다.

int8_t PitchSemitone = 3; // 3이 중립

//==========================

//======== 튜너 모드 함수 ==============
uint16_t TunerBaseFreq = 440;

volatile float TunerMeasurement; ///DSP로부터 받아옴

uint32_t TunerCalibrationValue = 23810;

uint16_t CurrnetTunerNote = 0;
uint16_t CurrentTunerCent = 0;

/*** [ADD][TUNER SENS & OVERLOAD UI] ***/
volatile uint8_t g_tnr_sens = 1;              // 0=LOW, 1=MID, 2=HIGH (기본 MID)
static   uint32_t s_tnr_sens_toast_until_ms = 0;
extern volatile uint8_t g_tnr_overload;       // notch.c에서 세팅

// ★ TNR CAL 편집용 가속 스텝 (1,10,50,100,500,1000)
static const uint16_t kTnrSteps[] = {1, 10, 50, 100, 500, 1000};
static uint8_t tnrStepIdx = 3; // 기본 100부터 시작

// ★ 외부 심볼(프로토타입/전역) 선언
extern uint32_t TunerCalibrationValue;
void notch_tuner_set_fs_trim_ppm(uint32_t ppm);

volatile uint8_t g_tuner_ui_on = 0;   /* 지금 UI가 튜너 화면인지 */

//==========================


//===시계
extern volatile uint8_t g_rtc_hour;
extern volatile uint8_t g_rtc_min;


//======== 튜너 모드 함수 ==============
uint16_t MetronomeBPM = 130;
uint8_t TimeSignature = 4;
uint8_t TimeSignatureDen = 4;     // ★ 새로 추가: 분모
uint8_t  MetronomeSubdiv = 1;
uint8_t IsMetronomeReady = 0;
float METRONOME_LENGTH = 2048;
float METRONOME_HI_CLICK = 1600;
float METRONOME_LO_CLICK = 700;
float METRONOME_TAU_MILISECONDS  = 5.5;
float METRONOME_HI_GAIN  = 0.95;
float METRONOME_LO_GAIN  = 0.70;
/* === Metronome beat counters === */

static uint32_t s_atempo_until_ms = 0;
/* a tempo 앵커: 재생 ON 순간의 BPM을 래치 */
static uint16_t g_met_atempo_anchor_bpm = 0;

/* === [NEW] RIT/ACC 길이 전역 설정 (1~16박, 기본 4박) === */
volatile uint8_t g_met_rit_accel_beats = 4;

/* === [NEW] A TEMPO 즉시 반영 플래그 ===
   1로 세팅되면 타이밍 루프에서 바로 다음 박으로 커밋 */
volatile uint8_t g_met_atempo_force_nextbeat = 0;

static uint8_t g_met_tap_prev_apir_saved = 0;
static uint8_t g_met_tap_prev_apir = 0;

uint8_t CurrentNoteIndex = 48;  // A4 (440Hz) 기준
float SoundFrequencyOutput;
uint8_t IsSoundGenReady = 0;
uint8_t SoundGenMode = 0; // 사인파 0 사각파 1 삼각파 2


UIState PrevUIBeforeVolume;    // 볼륨 진입 직전 화면 저장
uint32_t LastVolumeInteractionTick;   // SYSTIMER1 카운터 기준
uint32_t LastBalanceInteractionTick;  // SYSTIMER1 카운터 기준

volatile uint16_t MasterVolume = 10;
volatile uint16_t SoundBalance = 10;
volatile uint16_t MetronomeVolume = 30;
volatile uint16_t SFXVolume = 5;

uint16_t CurrentErrorStatus = 0;


// 토스트 활성화 메시지
volatile uint8_t  s_modeToastActive   = 0;
volatile uint32_t s_modeToastDeadline = 0;

volatile uint8_t g_led_brightness_level = 2; // LED 스트립 밝기

// === [AUTO VU: setting & idle tick] =========================================
// UI 설정에서 0/1로만 토글될 전역. 기본 0(꺼짐).
volatile uint8_t AutoVU_After10s = 0;   // 0: OFF, 1: ON

// Practice 화면에서의 "마지막 UI 상호작용 시각" (HAL_GetTick() 기준 ms)
static uint32_t g_practice_last_ui_ms = 0;


// 마이크 쪽 스위치
volatile int8_t  MicBoost_dB = 0;  // 기본 +0 dB
volatile uint8_t MicAGC_On   = 0;  // 기본 OFF
volatile uint16_t MicInputMode = 0; // 0=Stereo, 1=L 확장, 2=R 확장  ← ★ 추가


volatile uint8_t TestModeEnabled = 0; // 1이면 테스트 모드로 진입하기.
volatile uint8_t FlashDebug = 0; // 1이면 플래시테스트로 진입하기.



//================ BATTERY / SENSOR VALUES

#define USE_ADC_DMA           0   // 0=ADC IT, 1=ADC DMA(옵션)
#define BATT_ADC_OVERSAMPLE   1   // IT/DMAboth: 1로 두면 1샘플

//배터리 램프 임계입니다. 배터리 램프 임계는 BatteryTemp.C와 이 영역을 동시에 수정하십시오. 제발.
#ifndef BATT_WARN_MV
#define BATT_WARN_MV  1500u   // 빨강 ON 임계
#endif
#ifndef BATT_CRIT_MV
#define BATT_CRIT_MV  1000u   // 빨강 BLINK 임계
#endif
#ifndef BATT_SHDN_MV
#define BATT_SHDN_MV  200u   // 빨강 BLINK 임계
#endif
#ifndef BATT_SHDN_HOLD_MS
#define BATT_SHDN_HOLD_MS      300u   // CRIT 이하가 이 시간 연속 유지될 때만 셧다운
#endif
#ifndef BATT_SHDN_RELEASE_MV
#define BATT_SHDN_RELEASE_MV    50u   // 회복 판정 히스테리시스(노이즈/스파이크 억제)
#endif


volatile uint16_t MainBattADC = 4000;

volatile uint16_t Temperature = 000;
volatile uint16_t Humidity = 00;
volatile uint32_t adctimer = 0;

volatile uint8_t g_dht_req = 0;

#if USE_ADC_DMA
static uint16_t s_adc_dma_buf;
#endif


// 온도 단위 플래그: 0=섭씨, 1=화씨(표시만 바꿈)
volatile uint8_t TempUnitF = 0;
// 마지막 DHT22 측정 실패/이상값 플래그: 1=FAIL
volatile uint8_t DHT22_FAIL = 0;
// -------------------- 보정 파라미터(기본: 무보정) --------------------
volatile float Cal_TempC_Offset = 0.0f;  // °C 오프셋
volatile float Cal_TempC_Scale  = 1.0f;  // 온도 스케일(1.0 = 무보정)
volatile float Cal_RH_Offset    = 0.0f;  // %RH 오프셋
volatile float Cal_RH_Scale     = 1.0f;  // 습도 스케일(1.0 = 무보정)
// --------------------------------------------------------------------


///====================== 전원스위치 ==============================

// 전원 스위치: PA5 (HIGH=ON, LOW=OFF 가정)
#define POWER_SW_GPIO_Port   GPIOB
#define POWER_SW_Pin         GPIO_PIN_5

typedef enum { PWR_STATE_ON = 0, PWR_STATE_OFF } pwr_state_t;
static volatile pwr_state_t g_pwr_state = PWR_STATE_ON;

// 스위치 디바운스
static uint32_t g_sw_last_tick = 0;
static uint8_t  g_sw_stable    = 1;   // 기본은 ON(=HIGH)로 시작
static const uint32_t SW_DB_MS = 200;

static uint8_t  g_sw_stable;
static uint8_t  last_read;


// main.c 상단 어딘가(전역)
static volatile uint8_t g_ps_active = 0;   // POWER SAVE 동작중
static volatile uint8_t g_ps_wake = 0;   // POWER SAVE 동작중
static volatile uint8_t g_ps_tick   = 0;   // 1분 주기 알림
extern volatile uint8_t g_dht_req;


///==================================================================

///================ VU METER ===================================
// ===== VU Meter App (Practice 전용) =====
#define VU_FPS                 25u        // 최소 20 이상 권장
#define VU_REFRESH_MS          (1000u / VU_FPS)
// "볼륨 다이얼 버튼" 인덱스: 필요시 0/1/2 중 맞게 바꿔 써.
// 현재 PracticeHome에서 buttonEvents[0]은 재생/일시정지 토글로 쓰고 있으니 충돌 피하려 기본 1로 둠.
#define VU_TOGGLE_BTN_IDX      1

static uint8_t  g_vu_active    = 0;
static uint8_t  g_vu_inited    = 0;
static uint32_t g_vu_next_ms   = 0;
static uint8_t  g_vu_prev_color= 2;   // 탈출시 복귀할 색

#define VU_YELLOW_SEG  15    // 초록 임계 (≈ -10 dBFS 근처 느낌), 이 범위부터 '적절한 범위의 입력' 으로 취급 가능하
#define VU_RED_SEG     29     // 빨강 임계 (피크 근접)

// 사용자 제공 패턴(커스텀 문자)
// 슬레이브 API (notch.c)
void notch_set_vu_enabled(uint8_t on);
void notch_get_vu_segments(uint8_t *L, uint8_t *R);

// === [Mini VU용 커스텀 슬롯 예약] =========================================
// 항상 6,7번 슬롯은 미니 VU 전용(반칸/풀칸)으로 고정
#define VU_HALF_IDX     6
#define VU_FULL_IDX     7

// 피치 화면 전용(필요시만 사용) — 기존과 충돌 방지
#define PITCH_BAR_IDX   3
#define PITCH_MARK_IDX  4





// ===== [COMMON MINI-VU CONFIG] (BEGIN) =====
#ifndef MINI_VU_COMMON_CONFIG_H
#define MINI_VU_COMMON_CONFIG_H

// 0dBFS 기준 세그먼트 풀스케일
#define VU_SEG_MAX        30u      // -60..0 dBFS → 0..30 필없시 건들ㄴㄴ
#define VU_GATE_TH_ON     10u      // 1/3 지점에서 점등 시작
#define VU_GATE_TH_OFF    9u       // 히스테리시스 OFF 임계
#define VU_ALPHA_ATTACK   0.95f    // 상승(attack) EMA
#define VU_ALPHA_RELEASE  0.75f    // 하강(release) EMA

// 6칸(=12 half-seg) LCD 바 폭
#define VU_LCD_WIDTH      6u

// 그리기용 상태(게이트/EMA) — 바마다 독립 인스턴스 사용
typedef struct {
    uint8_t gate_on;
    float   ema;     // 0..1
} MiniVUState;

// 공통 변환: seg30(0..30) → (게이트/EMA) → halfSeg(0..12)
// - 게이트 OFF면 0 반환
// - 게이트 ON이면 (seg30-10)/20 → EMA → round( *12 )
static inline uint8_t MiniVU_Seg12_From_Seg30(uint8_t seg30, MiniVUState *st)
{
    if (!st) return 0;

    // Gate toggle
    if (!st->gate_on) {
        if (seg30 >= VU_GATE_TH_ON) st->gate_on = 1;
    } else {
        if (seg30 <= VU_GATE_TH_OFF) st->gate_on = 0;
    }

    // Target 0..1
    float target = 0.0f;
    if (st->gate_on) {
        int s = (seg30 > VU_GATE_TH_ON) ? (seg30 - VU_GATE_TH_ON) : 0; // 0..20
        float norm = (float)s / (float)(VU_SEG_MAX - VU_GATE_TH_ON);    // 0..1
        if (norm < 0.f) norm = 0.f; else if (norm > 1.f) norm = 1.f;
        target = norm;
    }

    // EMA
    float alpha = (target > st->ema) ? VU_ALPHA_ATTACK : VU_ALPHA_RELEASE;
    st->ema = (1.0f - alpha) * st->ema + alpha * target;

    // seg12
    int seg12 = (int)(st->ema * 12.0f + 0.5f);
    if (seg12 < 0) seg12 = 0; else if (seg12 > 12) seg12 = 12;
    return (uint8_t)seg12;
}

// 공백(6칸)으로 내부 지우기
static inline void MiniVU_Clear6(void *lcd, uint8_t row, uint8_t start_col)
{
    LCD16X2_Set_Cursor(lcd, row, start_col);
    LCD16X2_Write_String(lcd, "      "); // 6 spaces
}

#endif // MINI_VU_COMMON_CONFIG_H
// ===== [COMMON MINI-VU CONFIG] (END) =====







// 모드별로 필요한 커스텀 문자만 ‘완전 초기화 후’ 재등록
static void Practice_RegisterChars_ForMode(uint8_t mode)
{
    LCD16X2_ClearCustomChars(0);

    // 공통: 재생/일시정지
    LCD16X2_RegisterCustomChar(0, 0, Playing);
    LCD16X2_RegisterCustomChar(0, 1, Paused);

    if (mode == 0) {                  // DIRECT OUT
        // 2,1 아이콘: Loopback(3)
        LCD16X2_RegisterCustomChar(0, 3, Loopback);
        // (2,1에 Loopback은 UpdateSecondLine()에서 그림)
        LCD16X2_RegisterCustomChar(0, 4, clock_icon);   // ← 추가

    } else if (mode == 1) {           // NOTCH / PRESET
        // 2,1 아이콘: MelodyIcon(4)
        LCD16X2_RegisterCustomChar(0, 4, MelodyIcon);
    } else {                          // PITCH SHIFT
        // 피치 UI용 + 2,1 아이콘 TuningFork(5)
        LCD16X2_RegisterCustomChar(0, 2, flaticon);
        LCD16X2_RegisterCustomChar(0, 5, TuningFork);
        LCD16X2_RegisterCustomChar(0, 3, PitchShiftBar);   // 게이지 바
        LCD16X2_RegisterCustomChar(0, 4, PitchShiftCheck); // 선택 마커
    }

    // 항상 6,7은 미니 VU
    LCD16X2_RegisterCustomChar(0, 6, HalfFilledVU);
    LCD16X2_RegisterCustomChar(0, 7, FullFilledVU);
}


// i2s3(마이크) 합산 VU(0..8)를 notch에서 얻어옴(기존 구현 사용)
extern uint8_t notch_get_mic_vu8(void);

// 1행 13~16칸 미니 VU: 토스트여부 무시, VU 앱이 켜지면 숨김
// 1행 11~16칸 미니 VU: 토스트여부 무시, VU 앱이 켜지면 숨김
// 1행 10~16: '2' + [11~16] I2S3(마이크) 미니 VU (6칸), VU 앱 ON이면 숨김
// === REPLACE WHOLE FUNCTION ===
// 1행 10~16: I2S3(마이크) 미니 VU (6칸), VU 앱 ON이면 숨김
// === REPLACE WHOLE FUNCTION ===
// 1행 10~16: I2S3(마이크) 미니 VU (6칸), VU 앱 ON이면 숨김
static inline void Practice_DrawMiniVU_1stRow_13_16(void)
{
    if (g_vu_active) return; // 전용 VU 앱 가동 중엔 미니 VU 정지

    // 라벨: 9열 '[' , 16열 ']'
    LCD16X2_Set_Cursor(MyLCD, 1, 9);
    LCD16X2_Write_String(MyLCD, "[");
    LCD16X2_Set_Cursor(MyLCD, 1, 16);
    LCD16X2_Write_String(MyLCD, "]");

    const uint8_t VU_START_COL = 10;
    const uint8_t VU_WIDTH     = VU_LCD_WIDTH;

    // 마이크 볼륨 0이면 MUTE (기존 동작 유지)
    if (SoundBalance == 0) {
        LCD16X2_Set_Cursor(MyLCD, 1, VU_START_COL);
        LCD16X2_Write_String(MyLCD, " MUTE ");
        return;
    }

    // 입력(0..30) 취득: 마이크(모노)
    uint8_t segM = 0;
    notch_get_mic_vu_segments(&segM);   // << NEW: 0..30

    // 공통 게이트/EMA
    static MiniVUState s_state = {0, 0.0f};
    uint8_t seg12 = MiniVU_Seg12_From_Seg30(segM, &s_state);

    // ★ 임계 미만(게이트 OFF)이면 내부 비우고 조용히 종료
    if (!s_state.gate_on || seg12 == 0) {
        MiniVU_Clear6(MyLCD, 1, VU_START_COL);
        return;
    }

    // 12 half-seg → 칸 그리기
    uint8_t full = (seg12 >> 1);
    uint8_t half = (seg12 & 1);

    for (uint8_t i = 0; i < VU_WIDTH; ++i) {
        uint8_t col = (uint8_t)(VU_START_COL + i);
        if (i < full) {
            LCD16X2_DisplayCustomChar(0, 1, col, VU_FULL_IDX);
        } else if (i == full && half) {
            LCD16X2_DisplayCustomChar(0, 1, col, VU_HALF_IDX);
        } else {
            LCD16X2_Set_Cursor(MyLCD, 1, col);
            LCD16X2_Write_String(MyLCD, " ");
        }
    }
}

/*
 * 내부 스케일이 -60 dBFS…0 dBFS → 0…30
 * 세그로 선형이야(세그 1개 ≈ 2 dB). LCD는 2세그 = 1칸으로 15칸을 그려. 그래서 패널에 **“-60 —— -30 —— 0”**을
 * 동일 간격으로 인쇄하면 수학적으로도 딱 맞다(왼쪽 끝 -60, 가운데 -30, 오른쪽 끝 0).
 *
 * 미니 바는 **게이트 ON(-40 dBFS)**부터 0 dBFS까지의 상위 40 dB만 6칸(=12 half-seg)으로 선형 매핑한다.
 * 그래서 패널엔 **“-40 — -20 — 0”**을 동일 간격으로 인쇄하면 된다(왼쪽 -40, 중앙 -20, 오른쪽 0).
 * 이 구간에서 하프칸 ≈ 3.33 dB, 풀칸 ≈ 6.67 dB로 대응. 게이트 아래(-40 미만)는 설계상 표시 안 됨이 맞다.
 *
 * 풀스크린: -60 / -30 / 0 동일 간격 ✅ 미니 VU: -40 / -20 / 0 동일 간격 ✅
 *
 */

// 외부에서 제공되는 I2S2 입력 VU(L/R, 0..8)를 받기 위한 getter
extern void notch_get_vu_segments(uint8_t *outSegL, uint8_t *outSegR);

// === REPLACE WHOLE FUNCTION ===
// 1행 1~7: I2S2(음악) 미니 VU (6칸), VU 앱 ON이면 숨김
// === REPLACE WHOLE FUNCTION ===
// 1행 1~7: '1' + [2~7] I2S2(음악) 미니 VU (6칸), VU 앱 ON이면 숨김
static inline void Practice_DrawMiniVU_I2S2_1stRow_1_7(void)
{
    if (g_vu_active) return; // 전용 VU 앱 가동 중엔 미니 VU 정지

    // 라벨: 1열에 '[' , 8열에 ']'
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "[");
    LCD16X2_Set_Cursor(MyLCD, 1, 8);
    LCD16X2_Write_String(MyLCD, "]");

    const uint8_t VU_START_COL = 2;
    const uint8_t VU_WIDTH     = VU_LCD_WIDTH;

    // 입력(0..30) 취득: 스테레오 최대
    uint8_t segL = 0, segR = 0;
    notch_get_vu_segments(&segL, &segR);
    uint8_t seg30 = (segL > segR) ? segL : segR;

    // 공통 게이트/EMA
    static MiniVUState s_state = {0, 0.0f};
    uint8_t seg12 = MiniVU_Seg12_From_Seg30(seg30, &s_state);

    // ★ 임계 미만(게이트 OFF)이면 내부 비우고 조용히 종료
    if (!s_state.gate_on || seg12 == 0) {
        MiniVU_Clear6(MyLCD, 1, VU_START_COL);
        return;
    }

    // 12 half-seg → 칸 그리기
    uint8_t full = (seg12 >> 1);
    uint8_t half = (seg12 & 1);

    for (uint8_t i = 0; i < VU_WIDTH; ++i) {
        uint8_t col = (uint8_t)(VU_START_COL + i);
        if (i < full) {
            LCD16X2_DisplayCustomChar(0, 1, col, VU_FULL_IDX);
        } else if (i == full && half) {
            LCD16X2_DisplayCustomChar(0, 1, col, VU_HALF_IDX);
        } else {
            LCD16X2_Set_Cursor(MyLCD, 1, col);
            LCD16X2_Write_String(MyLCD, " ");
        }
    }
}





///================ ============ ===================================



/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2S2_Init(void);
static void MX_I2S3_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM5_Init(void);
static void MX_TIM6_Init(void);
static void MX_TIM1_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM7_Init(void);
static void MX_TIM8_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM9_Init(void);
static void MX_TIM12_Init(void);
/* USER CODE BEGIN PFP */


extern void Tuner_Task(void);
extern void notch_set_tuner_enabled(uint8_t on);   // 튜너 on/off를 UI랑 묶으려고 씀






//UI 관련 렌더링 함수들
void RenderUI(void);
void RenderPracticeUI(void);
void RenderPracticeCutoffUI(void);
void RenderTunerUI(void);
void RenderMetronomeUI(void);
void RenderSoundGenUI(void);
void RenderSettingsUI(void);
void RenderModeUI(void);

//각 UI 상태 진입용 함수들
void WelcomeScreen(void);
void ModeSelection(void);
void PracticeHome(void);
void PracticeFreqSettingCutoffStart(void);
void PracticeFreqSettingCutoffEnd(void);
void PracticeInstPresetSetting(void);
void TunerIntro(void);
void TunerHome(void);
void TunerBaseAFreqSetting(void);
void MetronomeModeIntro(void);
void MetronomeHome(void);
void MetronomeBPMSetting(void);
void MetronomeTimeSignatureSetting(void);
void MetronomeTimingCalc(void);
void SoundGenIntro(void);
void SoundGenHome(void);
void SoundGenBaseAFreqSetting(void);
void SettingsHome(void);
void SettingsHelp(void);
void SettingsPowerOffConfirm(void);
void SettingsAbout(void);
void SettingsFWUpdate(void);

// UI 상태 전환 함수
void UI_NextState(void);
void UI_PrevState(void);
void UI_Enter(void);
void UI_Back(void);

// 로터리 이벤트 처리기
void Handle_Rotary_Event(RotaryEvent event);
void HandlePracticeModeRotary(RotaryEvent event);
void HandleTunerModeRotary(RotaryEvent event);
void HandleMetronomeModeRotary(RotaryEvent event);
void HandleSoundGenModeRotary(RotaryEvent event);

// 버튼 이벤트 처리기
void UI_HandleShortPress(uint8_t btn_id);
void UI_HandleLongPress(uint8_t btn_id);

// 입력 디바이스 폴링 함수
void Poll_Rotary(TIM_HandleTypeDef *htim, int16_t *prev_count, RotaryEvent *eventFlag);
void Poll_RotaryButtons(void);

// 기타 유틸리티
void LCDColorSet(uint8_t LCDColor);

void Tuner_Update_Frequency(float newFreq);
extern void notch_metronome_click(uint8_t accent);
extern void notch_metronome_subclick(void);  // 🆕 서브비트

extern void notch_set_tuner_enabled(uint8_t on);

// main.c 상단 (또는 공용 헤더)
void notch_ui_rotary_click_freq(float freq_hz);
void notch_ui_rotary_set_params(float tau_ms, float gain); // 선택
void notch_ui_button_beep(void);
void notch_ui_button_sfx_enable(uint8_t on);



void EnterFirmwareUpdateMode(void);
void ConfigStorage_Service(uint8_t trigger_save);  // 0=load, 1=save-if-changed

void ccm_overlay_boot_select(int testmode_on);

void Measure_Service(void);
void BatteryTemp_HeaderService(void);
void BatteryTemp_HeaderMarkDirty(void);

static void BootCheck_FlashDebug(void)
{
    // 액티브-로우: 눌림 = RESET
    const uint32_t HOLD_MS   = 3000;   // 길게 눌러야 하는 시간
    const uint32_t POLL_STEP = 5;      // 폴링 간격
    uint8_t  pressed = 0;
    uint32_t t0 = 0;

    // 1) 버튼 현재 상태 확인
    uint8_t lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2) == GPIO_PIN_RESET);

    // 2) 눌려있지 않으면 즉시 반환 (부팅 지연 없음)
    if (!lvl) return;

    // 3) 눌려 있으면 HOLD_MS 동안 유지되는지 폴링
    pressed = 1;
    t0 = HAL_GetTick();
    while (pressed && (HAL_GetTick() - t0) < HOLD_MS) {
        // 계속 눌림 유지되는지 확인
        lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2) == GPIO_PIN_RESET);
        if (!lvl) { pressed = 0; break; }
        HAL_Delay(POLL_STEP);
    }

    // 4) 정말 1초 내내 눌렸다면 → 디버그 모드 ON + 토스트 표시
    if (pressed) {
        extern volatile uint8_t FlashDebug; // main.c 전역
        FlashDebug = 1;

        // 정보 색(Blue=4)로 토스트
        LCDColorSet(4);
        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "FLASH DEBUG   ");
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, "ENABLED!      ");
        HAL_Delay(1000); // 안내 1초 유지
    }
}


void BootCheck_TestMode(void)
{
    // 액티브-로우: 눌림 = RESET
    const uint32_t HOLD_MS   = 3000;   // 길게 눌러야 하는 시간
    const uint32_t POLL_STEP = 5;      // 폴링 간격
    uint8_t  pressed = 0;
    uint32_t t0 = 0;

    // 1) 버튼 현재 상태 확인 (PE3 사용)
    uint8_t lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET);

    // 2) 눌려있지 않으면 즉시 반환 (부팅 지연 없음)
    if (!lvl) return;

    // 3) 눌려 있으면 HOLD_MS 동안 유지되는지 폴링
    pressed = 1;
    t0 = HAL_GetTick();
    while (pressed && (HAL_GetTick() - t0) < HOLD_MS) {
        // 계속 눌림 유지되는지 확인
        lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET);
        if (!lvl) { pressed = 0; break; }
        HAL_Delay(POLL_STEP);
    }

    // 4) 정말 1초 내내 눌렸다면 → 테스트 모드 ON + 토스트 표시
    if (pressed) {
        extern volatile uint8_t TestModeEnabled; // main.c 전역 (초기값 0)  :contentReference[oaicite:3]{index=3}
        TestModeEnabled = 1;

        // 정보 색(Blue=4)로 토스트 (원본과 동일 스타일)  :contentReference[oaicite:4]{index=4}
        LCDColorSet(4);
        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "TEST MODE    ");
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, "ENABLED!     ");
        HAL_Delay(1000); // 안내 1초 유지
    }
}

static void BootCheck_FWUpdate(void)
{
    // 액티브-로우: 눌림 = RESET
    const uint32_t HOLD_MS   = 3000;   // 길게 눌러야 하는 시간
    const uint32_t POLL_STEP = 5;      // 폴링 간격

    // 1) 버튼 현재 상태 확인 (PE4 사용)
    uint8_t lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET);
    if (!lvl) return;  // 눌려있지 않으면 즉시 반환

    // 2) 눌림 유지 확인
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < HOLD_MS) {
        lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET); // ★ PE4로 일관
        if (!lvl) return;                  // 중간에 떼면 취소
        HAL_Delay(POLL_STEP);
    }

    // 3) 1초 내내 눌렸다면 → 펌웨어 업데이트 모드 진입 (no-return)
    EnterFirmwareUpdateMode();
    while (1) { /* no-return safety */ }
}

static void BootCheck_RESETMode(void)
{
    // 액티브-로우: 눌림 = RESET
    const uint32_t HOLD_MS   = 1000;   // 길게 눌러야 하는 시간
    const uint32_t POLL_STEP = 5;      // 폴링 간격

    // 1) 버튼 현재 상태 확인 (PE4 사용)
    uint8_t lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET);
    if (!lvl) return;  // 눌려있지 않으면 즉시 반환

    // 2) 눌림 유지 확인
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < HOLD_MS) {
        lvl = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET); // ★ PE4로 일관
        if (!lvl) return;                  // 중간에 떼면 취소
        HAL_Delay(POLL_STEP);
    }

    // 3) 1초 내내 눌렸다면 → 펌웨어 업데이트 모드 진입 (no-return)
    EnterResetFlash();
}


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

///====================================
static uint8_t g_lcd_last_color = 2;  // 전역(파일 상단 USER 영역)에 선언해도 OK

void LCDColorSet(uint8_t LCDColor){
	g_lcd_last_color = LCDColor;  // ★ 지금 설정한 색을 기억

	switch (LCDColor){

	case 0: /// PINK
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 1);
		break;
	case 1: /// YELLOW
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 1);
		break;
	case 2:  // SKY
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 0);
		break;
	case 3: /// G
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 0);
		break;
	case 4:   /// B
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 0);
		break;
	case 5:  ///R
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 1);
		break;
	case 6: // WHITE COLOR
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 1);
		break;
	case 7: // OFF
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 0);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 0);
		break;
	default:
		break;

	}

}

// === 임계 전압 이하 시: 저장 → 안내 → 백라이트 OFF → STOP 진입(복구 없음) ===
static inline uint16_t batt_mv_from_adc(uint16_t raw12)
{
    // 12-bit ADC, Vref=3.3V (LED 판정식과 동일)
    return (uint16_t)(((uint32_t)raw12 * 3300u + 2047u) / 4095u);
}

void Battery_CheckAndShutdownIfLow_Blocking(void)
{
    static uint32_t low_since_ms = 0;   // CRIT 이하 유지 시작 시각(0은 미시작)
    uint16_t mv = batt_mv_from_adc(MainBattADC);
    uint32_t now = HAL_GetTick();

    // ── ① 판정 영역 ────────────────────────────────────────────────
    // A) CRIT 이하: 타이머 시작/유지
    if (mv <= BATT_SHDN_MV) {
        if (low_since_ms == 0) low_since_ms = now;         // 처음 떨어졌다면 스탬프
        if ((now - low_since_ms) < BATT_SHDN_HOLD_MS) {
            return; // 아직 홀드시간 안 됨 → 셧다운 보류
        }
        // 여기 도달 = CRIT 이하가 BATT_SHDN_HOLD_MS 이상 유지됨 → 셧다운 수행
    }
    // B) 회복 여부: 히스테리시스 고려해서 리셋
    else if (mv >= (uint16_t)(BATT_SHDN_MV + BATT_SHDN_RELEASE_MV)) {
        // CRIT + 히스테리시스 이상이면 "충분히 회복"으로 간주 → 타이머 리셋
        low_since_ms = 0;
        return; // 정상 상태, 셧다운 안 함
    } else {
        // CRIT < mv < CRIT + RELEASE_MV → 회복 애매한 구간: 타이머 유지(리셋하지 않음)
        return; // 셧다운도, 리셋도 하지 않음
    }

    // ── ② 여기서부터는 실제 셧다운 루틴 ───────────────────────────
    // 안내(1초)
    LCD16X2_Clear(MyLCD);
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "BATTERY LOW!");
    LCD16X2_Set_Cursor(MyLCD, 2, 1);
    char line2[17];
    snprintf(line2, sizeof(line2), "%4umV  SHUTDOWN", (unsigned)mv);
    LCD16X2_Write_String(MyLCD, line2);
    HAL_Delay(1000);

    // 저장(조용히)
    extern volatile uint8_t FlashDebug;
    uint8_t prevDbg = FlashDebug; FlashDebug = 0;
    ConfigStorage_Service(1);
    FlashDebug = prevDbg;

    // 표시 끄기
    LCDColorSet(7);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);

    // 복구 없이 STOP
    HAL_SuspendTick();
    __disable_irq();
    while (1) {
        HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
        __WFI();
    }
}


void OperationLEDSet(uint8_t LEDMode /* unused */)
{

    // 12-bit ADC, Vref=3.3V → mV 환산(프로젝트 내 사용식과 동일)
    uint16_t mv = batt_mv_from_adc(MainBattADC);  // ← 여기만 교체
    // 전압 기준 3단계 판정
    uint8_t mode;
    if      (mv <= BATT_CRIT_MV) mode = 2;  // 매우 낮음: 빨강 깜빡
    else if (mv <= BATT_WARN_MV) mode = 1;  // 낮음: 빨강 고정
    else                         mode = 0;  // 정상: 초록 고정

    switch (mode) {
    case 0: // 초록 ON, 빨강 OFF
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
        break;

    case 1: // 초록 OFF, 빨강 ON
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
        break;

    case 2: // 초록 OFF, 빨강 1Hz BLINK
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        // 비교 연산자(==) 사용, 깜빡임 대상은 RED(PB9)
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9,
                          (UITotalBlinkStatus == 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        break;

    default:
        // 안전: 둘 다 OFF
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
        break;
    }
}
//====================================================
// === 버튼 스캐너 (1kHz 레벨 기반) ===
// 기존 UI 변수/이벤트 이름(buttonEvents 등) 그대로 사용
#define BTN_DEBOUNCE_MS   10u        // 10ms 안정화 후 상태 결정 (원하는 값으로 튜닝)
#define LONG_PRESS_MS     1000u      // 기존 값 유지

static uint8_t  btn_level_hw[3] = {0,0,0};  // 즉시 읽은 HW 레벨(1=눌림)
static uint8_t  btn_stable[3]   = {0,0,0};  // 디바운싱 통과한 안정 레벨
static uint16_t btn_cnt_ms[3]   = {0,0,0};  // 안정 카운터
static uint32_t btn_down_tick[3]= {0,0,0};  // 눌림 시작 시각
static uint8_t  btn_long_sent2[3]= {0,0,0}; // 길게 이벤트 중복 방지

static inline uint8_t read_btn_level(int i) {
    switch(i){
        case 0: return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2) == GPIO_PIN_RESET); // active-low
        case 1: return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET);
        case 2: return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET);
        default: return 0;
    }
}

static void Buttons_Scan_1ms(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 3; ++i) {
        uint8_t lvl = read_btn_level(i);        // 0=놓임, 1=눌림
        if (lvl == btn_level_hw[i]) {
            if (btn_cnt_ms[i] < BTN_DEBOUNCE_MS) btn_cnt_ms[i]++;
        } else {
            btn_level_hw[i] = lvl;              // 레벨 바뀜 → 카운터 리셋
            btn_cnt_ms[i]   = 0;
        }

        // 디바운싱 완료 순간(연속 BTN_DEBOUNCE_MS ms 유지)
        if (btn_cnt_ms[i] == BTN_DEBOUNCE_MS) {
            if (btn_stable[i] != btn_level_hw[i]) {
                btn_stable[i] = btn_level_hw[i];

                if (btn_stable[i]) {
                    // 안정된 '눌림'이 막 시작됨
                    btn_down_tick[i] = now;
                    btn_long_sent2[i] = 0;
                } else {
                    // 안정된 '놓임'으로 전이됨 → 짧게 판정
                	// 버튼이 눌리면 save합니다.
                    uint32_t dt = now - btn_down_tick[i];
                    if (!btn_long_sent2[i] && dt < LONG_PRESS_MS) {
                        buttonEvents[i] = BUTTON_EVENT_SHORT_PRESS;
                        Powersave_RequestSave();
                    }
                }
            }
        }

        // 길게: 눌림 상태가 계속되고, LONG_PRESS_MS 경과 시 즉시 발행
        if (btn_stable[i] && !btn_long_sent2[i]) {
            if ((now - btn_down_tick[i]) >= LONG_PRESS_MS) {
                buttonEvents[i] = BUTTON_EVENT_LONG_PRESS;
                btn_long_sent2[i] = 1;
            }
        }
    }
}







void Poll_Rotary(TIM_HandleTypeDef *htim, int16_t *prev_count, RotaryEvent *eventFlag) {
    int16_t count = __HAL_TIM_GET_COUNTER(htim);
    int16_t diff = (int16_t)(count - *prev_count);

    // 너무 작은 값은 무시 (노이즈 방지 + 2틱씩만 반응)
    // 로터리가 돌아갈 떄 BKSRAM도 SAVE 합니다.
    if (diff >= 4) {
        *eventFlag = ROTARY_EVENT_CCW;
        *prev_count = count;
        Powersave_RequestSave();
    } else if (diff <= -4) {
        *eventFlag = ROTARY_EVENT_CW;
        *prev_count = count;
        Powersave_RequestSave();
    } else {
        *eventFlag = ROTARY_EVENT_NONE;
    }
}




///===================================================================


/*
 * A11, A12, B3 = DSP Board의 상태를 설정하는 PIN입니다. 각각 A11 A12 B3 순서로 이진수 코드화됨.
 * 각각의 CASE에서 해당 핀으로 GPIO_PIN이 Write됨.
 */

typedef enum {
	DSP_Mode_Standby,
	DSP_Mode_Practice,
	DSP_Mode_Tuner,
	DSP_Mode_Metronome,
	DSP_Mode_SoundGen,
	DSP_Mode_FirmwareUpdate,
	DSP_Mode_Reserved_1,
	DSP_Mode_Reserved_2


} DSPMode;

volatile DSPMode currentDSPmode = DSP_Mode_Standby;


// ────────────────────────────────────────────────────────────────
// ────────────────────────────────────────────────────────────────
// 1행 12~16열(5칸)에 XXX:X 표시. fixed_zeros==1이면 "000:0" 고정 포맷.
// 타임시그 분자 자리수에 따라 카운터 폭을 바꿔서 찍어준다.
volatile uint8_t g_met_counter_reset_on_start = 0;
static uint32_t s_next_sub    = 0;
static uint8_t  s_sub_steps   = 1;    // 이 박에서 쓸 step수
static uint8_t  s_sub_mask    = 0x01; // 이 박에서 쓸 mask
static uint8_t  s_sub_idx     = 0;    // 지금 몇번째 step을 보고 있는가
static uint32_t g_met_big_cnt   = 0; // 큰 박 누적 (표시: 0..999)
static uint8_t  g_met_small_cnt = 0; // 작은 박 (표시: 0..9; 0=초기)

volatile uint8_t g_metSubSteps;  // 1..8
volatile uint8_t g_metSubMask;   // 비트마스크
volatile uint8_t g_met_req_counter_reset = 0;


/* === [NEW] Metronome shared phase (Option A) === */
volatile uint32_t g_met_phi_q16    = 0;   // 0..65535 : 현재 박의 진행도
volatile uint8_t  g_met_beat_inbar = 0;   // 0..(TimeSignature-1)
volatile uint8_t  g_met_ready      = 0;   // 현재 메트로놈 on/off 미러

/* === [IMPROVED] Ritardando / Return state (with easing curves) === */

/* 외부에서 참조하는 모드값(표시/오버레이용) — 기존과 호환 */
#define MET_RIT_MODE_OFF     0
#define MET_RIT_MODE_RIT     1
#define MET_RIT_MODE_RETURN  2
#define MET_RIT_MODE_ACCEL   3   /* 화면용: 가속 진행 중 */
volatile uint8_t g_met_rit_mode = MET_RIT_MODE_OFF;  /* RIT 진행 중엔 RIT, 복귀 중엔 RETURN */

/* 커브 종류 */
#define MET_RIT_CURVE_LINEAR        0
#define MET_RIT_CURVE_EASE_IN_QUAD  1   /* u^2 — 리타는 이게 자연스러움 */
#define MET_RIT_CURVE_EASE_OUT_QUAD 2   /* 1-(1-u)^2 — 복귀(가속)엔 이게 부드러움 */
#define MET_RIT_CURVE_SMOOTHSTEP    3   /* 3u^2-2u^3 — 엔드 기울기 0 */

/* 내부 페이즈 */
#define _RIT_PHASE_OFF     0
#define _RIT_PHASE_RIT     1
#define _RIT_PHASE_HOLD    2
#define _RIT_PHASE_RETURN  3


/* === RIT/ACCEL 전역 설정 (박 수 + 곡선 프리셋) ======================= */

#ifndef MET_RIT_DEFAULT_BEATS
#define MET_RIT_DEFAULT_BEATS  4   /* 기본 4박 */
#endif

typedef enum {
    MET_RIT_PRESET_NORMAL = 0,
    MET_RIT_PRESET_SOFT   = 1,
    MET_RIT_PRESET_FAST   = 2,
    MET_RIT_PRESET_COUNT
} MetRitCurvePreset;

/* 몇 박에 걸쳐 rit/accel 할지 (리타·아첼 공용) */
volatile uint8_t g_met_rit_beats        = MET_RIT_DEFAULT_BEATS;   /* 1~8 정도 추천 */

/* 곡선 프리셋: NORMAL / SOFT / FAST */
volatile uint8_t g_met_rit_curve_preset = MET_RIT_PRESET_NORMAL;

/* Settings UI에서 표시용 이름 */
static const char* const kMetRitCurveName[MET_RIT_PRESET_COUNT] = {
    "NORMAL",
    "SOFT",
    "FAST"
};


/* ── Delta adjust (목표 BPM을 base대비 ±로 변경) ───────────────── */
#ifndef MET_DELTA_STEP_BPM
#define MET_DELTA_STEP_BPM   2    /* 로터리 한 칸당 델타 변화량 */
#endif

#ifndef MET_MAX_BPM
#define MET_MAX_BPM         300
#endif

#ifndef MET_MIN_BPM
#define MET_MIN_BPM          30
#endif

/* [NEW] RIT/ACCEL 기본 설정 */
#ifndef MET_RIT_FIXED_BEATS
#define MET_RIT_FIXED_BEATS    4   /* 리타/아첼에 사용할 고정 박자 수 */
#endif

#ifndef MET_DELTA_AUTO_MIN_BPM
#define MET_DELTA_AUTO_MIN_BPM  4   /* 자동 델타 최소값 */
#endif

#ifndef MET_DELTA_AUTO_MAX_BPM
#define MET_DELTA_AUTO_MAX_BPM 40   /* 자동 델타 최대값 */
#endif

#ifndef MET_DELTA_AUTO_PCT
#define MET_DELTA_AUTO_PCT     10   /* 기준 BPM의 10%를 기본 델타로 사용 */
#endif





typedef struct {
    uint8_t  phase;          /* _RIT_PHASE_* */
    uint16_t base_bpm;       /* 시작 BPM (a tempo 기준) */
    uint16_t targ_bpm;       /* 리타 목표 BPM */
    /* rit */
    uint16_t rit_beats;      /* 리타에 사용할 박 수 (0이면 즉시 targ) */
    uint16_t rit_i;          /* 진행 beat 인덱스 */
    uint8_t  curve_rit;      /* 커브 */
    /* hold */
    uint16_t hold_beats;     /* 목표 BPM에서 머무는 박 수 */
    uint16_t hold_i;
    /* return */
    uint16_t ret_beats;      /* 복귀 박 수 (0이면 즉시 a tempo) */
    uint16_t ret_i;
    uint8_t  curve_ret;      /* 커브 */
} RitPlan;

static RitPlan s_rit = {0};

/* Q15 보간 유틸: u(0..32767) */
static inline uint16_t _q15_lerp_u16(uint16_t a, uint16_t b, uint16_t u_q15)
{
    /* a + (b-a)*u */
    int32_t da = (int32_t)b - (int32_t)a;
    uint32_t add = ((int64_t)da * (uint32_t)u_q15) >> 15;
    int32_t r = (int32_t)a + (int32_t)add;
    if (r < 0) r = 0;
    if (r > 65535) r = 65535;
    return (uint16_t)r;
}

/* 커브 적용: u_q15 -> f_q15 */
static inline uint16_t _ease_apply(uint16_t u, uint8_t curve)
{
    switch (curve) {
        default:
        case MET_RIT_CURVE_LINEAR:
            return u;
        case MET_RIT_CURVE_EASE_IN_QUAD: {
            uint32_t u2 = ((uint32_t)u * (uint32_t)u) >> 15;
            return (uint16_t)(u2 & 0xFFFF);
        }
        case MET_RIT_CURVE_EASE_OUT_QUAD: {
            uint32_t one = 32768u;
            uint32_t v = one - u;                  /* (1-u) */
            uint32_t v2 = (v * v) >> 15;           /* (1-u)^2 */
            uint32_t f = one - v2;                 /* 1-(1-u)^2 */
            return (uint16_t)(f & 0xFFFF);
        }
        case MET_RIT_CURVE_SMOOTHSTEP: {
            /* 3u^2 - 2u^3 */
            uint32_t u2 = ((uint32_t)u * u) >> 15;
            uint32_t u3 = (u2 * u) >> 15;
            int32_t  f  = (int32_t)(3*u2) - (int32_t)(2*u3);
            if (f < 0) f = 0;
            if (f > 32768) f = 32768;
            return (uint16_t)f;
        }
    }
}

/* 델타 조절: 현재 플랜(RIT/ACCEL)의 target BPM을 수정 */
static inline void Met_Rit_AdjustDelta_Signed(int8_t dir)
{
    /* 진행 중인 ‘메인 진행 페이즈’에서만 조절 */
    if (s_rit.phase != _RIT_PHASE_RIT) return;

    uint16_t base = s_rit.base_bpm ? s_rit.base_bpm
                                   : (MetronomeBPM ? MetronomeBPM : 60);
    uint16_t targ = s_rit.targ_bpm;

    /* 현재 델타 = |base - targ| */
    int32_t delta = (int32_t)((base > targ) ? (base - targ) : (targ - base));

    /* 새 델타 계산 */
    delta += (dir > 0 ? MET_DELTA_STEP_BPM : -MET_DELTA_STEP_BPM);
    if (delta < 0) delta = 0;

    /* 현재 모드(RIT/ACCEL)에 따라 target 재계산
       더 이상 base/targ 비교로 모드를 바꾸지 않는다. */
    switch (g_met_rit_mode) {
    case MET_RIT_MODE_RIT: {
        /* RIT: 느려지도록 base - delta */
        int32_t proposed = (int32_t)base - delta;
        if (proposed < MET_MIN_BPM) proposed = MET_MIN_BPM;
        s_rit.targ_bpm = (uint16_t)proposed;
        break;
    }
    case MET_RIT_MODE_ACCEL: {
        /* ACCEL: 빨라지도록 base + delta */
        int32_t proposed = (int32_t)base + delta;
        if (proposed > MET_MAX_BPM) proposed = MET_MAX_BPM;
        s_rit.targ_bpm = (uint16_t)proposed;
        break;
    }
    default:
        /* 진행 중이 아니면 조절 무시 */
        return;
    }

    /* 델타가 0이면 사실상 변화가 없으므로 플랜 종료 */
    if (s_rit.targ_bpm == base) {
        s_rit.phase    = _RIT_PHASE_OFF;
        g_met_rit_mode = MET_RIT_MODE_OFF;
    }
}

/* 편의 래퍼 */
void Met_Rit_DeltaUp(void)   { Met_Rit_AdjustDelta_Signed(+1); }  /* 델타 키우기 */
void Met_Rit_DeltaDown(void) { Met_Rit_AdjustDelta_Signed(-1); }  /* 델타 줄이기 */

/* 곡선 프리셋 → 실제 보간 커브 선택 */
static void Met_Rit_SelectCurves(uint8_t* out_curve_main, uint8_t* out_curve_ret)
{
    uint8_t preset = g_met_rit_curve_preset;
    if (preset >= MET_RIT_PRESET_COUNT) preset = MET_RIT_PRESET_NORMAL;

    switch (preset) {
    default:
    case MET_RIT_PRESET_NORMAL:
        /* 기본: 리타는 약간 끝에서 많이 변하는 느낌 (Ease-In),
                 복귀는 조금 더 직관적인 Ease-Out */
        *out_curve_main = MET_RIT_CURVE_EASE_IN_QUAD;
        *out_curve_ret  = MET_RIT_CURVE_EASE_OUT_QUAD;
        break;

    case MET_RIT_PRESET_SOFT:
        /* SOFT: 전체적으로 더 둥근 S-curve 느낌 */
        *out_curve_main = MET_RIT_CURVE_SMOOTHSTEP;
        *out_curve_ret  = MET_RIT_CURVE_SMOOTHSTEP;
        break;

    case MET_RIT_PRESET_FAST:
        /* FAST: 거의 직선에 가까운 균일 변화 */
        *out_curve_main = MET_RIT_CURVE_LINEAR;
        *out_curve_ret  = MET_RIT_CURVE_LINEAR;
        break;
    }
}








/* === 공개 API ===
   1) “리타 → (옵션)홀드 → (옵션)복귀” 전체 계획을 세팅 */
/* delta_bpm만큼 느려지는 리타르단도 플랜 시작 */
static void Met_Rit_StartPlan(uint16_t delta_bpm,
                              uint16_t rit_beats, uint16_t hold_beats, uint16_t ret_beats,
                              uint8_t  curve_rit,  uint8_t  curve_ret)
{
    uint16_t cur  = MetronomeBPM ? MetronomeBPM : 60;
    uint16_t base = g_met_atempo_anchor_bpm ? g_met_atempo_anchor_bpm : cur;
    uint16_t targ = (base > delta_bpm) ? (base - delta_bpm) : MET_MIN_BPM;

    s_rit.base_bpm   = base;
    s_rit.targ_bpm   = targ;
    s_rit.rit_beats  = rit_beats;
    s_rit.hold_beats = hold_beats;
    s_rit.ret_beats  = ret_beats;
    s_rit.curve_rit  = curve_rit;
    s_rit.curve_ret  = curve_ret;
    s_rit.rit_i = s_rit.hold_i = s_rit.ret_i = 0;
    s_rit.phase = _RIT_PHASE_RIT;

    g_met_rit_mode = MET_RIT_MODE_RIT;
}

/* RIT/ACCEL 자동 델타: 기준 BPM의 일정 비율을 사용 (BPM에 따라 가변) */
static uint16_t Met_Rit_ComputeAutoDelta(void)
{
    uint16_t cur  = MetronomeBPM ? MetronomeBPM : 60;
    uint16_t base = g_met_atempo_anchor_bpm ? g_met_atempo_anchor_bpm : cur;

    /* 기준 BPM의 MET_DELTA_AUTO_PCT% 를 기본 델타로 사용 */
    uint32_t delta = (uint32_t)base * MET_DELTA_AUTO_PCT + 50u; /* 반올림용 0.5 */
    delta /= 100u;

    if (delta < MET_DELTA_AUTO_MIN_BPM) delta = MET_DELTA_AUTO_MIN_BPM;
    if (delta > MET_DELTA_AUTO_MAX_BPM) delta = MET_DELTA_AUTO_MAX_BPM;

    return (uint16_t)delta;
}


/* 2) 간단 프리셋: “리타 4박 → 복귀 4박” (권장 곡선) */
/* 2) 간단 프리셋: “리타 고정 N박 (기본 4박)” — 델타는 BPM에 비례 */
/* 2) 간단 프리셋: “리타 N박 → (옵션)복귀” (N은 전역 설정) */
void Met_Rit_StartDelta(uint16_t delta_bpm)
{
    uint8_t curve_rit, curve_ret;
    Met_Rit_SelectCurves(&curve_rit, &curve_ret);

    /* g_met_rit_beats 박에 걸쳐 rit, hold/return은 지금은 0 */
    uint16_t beats = g_met_rit_beats;
    if (beats == 0) beats = 1;

    Met_Rit_StartPlan(delta_bpm,
                      /*rit*/beats, /*hold*/0, /*ret*/0,
                      curve_rit, curve_ret);
}



/* 아첼레란도: 현재 BPM에서 +delta만큼 N박에 걸쳐 가속, 끝나면 (옵션)복귀 */
/* delta_bpm만큼 빨라지는 아첼레란도 플랜 시작 */
static void Met_Accel_StartPlan(uint16_t delta_bpm,
                                uint16_t acc_beats, uint16_t hold_beats, uint16_t ret_beats,
                                uint8_t  curve_acc, uint8_t  curve_ret)
{
    uint16_t cur  = MetronomeBPM ? MetronomeBPM : 60;
    uint16_t base = g_met_atempo_anchor_bpm ? g_met_atempo_anchor_bpm : cur;
    uint16_t targ = base + delta_bpm;
    if (targ > MET_MAX_BPM) targ = MET_MAX_BPM;

    s_rit.base_bpm   = base;
    s_rit.targ_bpm   = targ;
    s_rit.rit_beats  = acc_beats;   /* 이름만 rit_beats 슬롯 재사용 */
    s_rit.hold_beats = hold_beats;
    s_rit.ret_beats  = ret_beats;
    s_rit.curve_rit  = curve_acc;
    s_rit.curve_ret  = curve_ret;
    s_rit.rit_i = s_rit.hold_i = s_rit.ret_i = 0;
    s_rit.phase = _RIT_PHASE_RIT;

    g_met_rit_mode = MET_RIT_MODE_ACCEL;
}


/* 간편 프리셋: 아첼 4박(Ease-In), 복귀 4박(Ease-Out) */

/* 3) “리타 4박 → 즉시 a tempo” */
/* 3) “아첼 고정 N박 (기본 4박)” — 델타는 BPM에 비례 */
/* 3) “아첼 N박 → (옵션)복귀” (N은 전역 설정) */
void Met_Accel_StartDelta(uint16_t delta_bpm)
{
    uint8_t curve_acc, curve_ret;
    Met_Rit_SelectCurves(&curve_acc, &curve_ret);

    uint16_t beats = g_met_rit_beats;
    if (beats == 0) beats = 1;

    Met_Accel_StartPlan(delta_bpm,
                        /*acc*/beats, /*hold*/0, /*ret*/0,
                        curve_acc, curve_ret);
}


/* 4) “지금 템포에서 a tempo로 ‘서서히’ 복귀만 시작” (ex. 4박 복귀) */
/* [REPLACE WHOLE FUNCTION] */
void Met_Rit_ReturnToBase(void)
{
    uint16_t atempo = g_met_atempo_anchor_bpm ? g_met_atempo_anchor_bpm
                                              : (MetronomeBPM ? MetronomeBPM : 60);
    MetronomeBPM = atempo;
    s_rit.phase = _RIT_PHASE_OFF;
    g_met_rit_mode = MET_RIT_MODE_OFF;

    /* 1초 동안 'ATEMPO' 토스트 */
    s_atempo_until_ms = HAL_GetTick() + 1000;

    /* 다음 박 강제 이동은 하지 않음 — 위상/박 경계 유지 */
    // g_met_atempo_force_nextbeat = 1;   // 제거
}



/* === 박 경계에서 한 번만 호출 ===
   리타/홀드/복귀 진행에 맞춰 MetronomeBPM을 ‘절대치’로 보간 */
/* === 박 경계에서 한 번만 호출 ===
   리타/홀드 진행에 맞춰 인덱스만 갱신 (모드 판단은 여기서 하지 않음) */
static inline void Metronome_RitOnBeatTick(void)
{
    switch (s_rit.phase) {
    default:
    case _RIT_PHASE_OFF:
        /* 플랜이 완전히 꺼진 상태 */
        g_met_rit_mode = MET_RIT_MODE_OFF;
        return;

    case _RIT_PHASE_RIT: {
        /* 진행 박수 업데이트 */
        if (s_rit.rit_beats == 0) {
            /* 0박이면 바로 HOLD 또는 OFF로 전환 */
            s_rit.phase = (s_rit.hold_beats > 0)
                        ? _RIT_PHASE_HOLD
                        : _RIT_PHASE_OFF;
        } else {
            if (s_rit.rit_i < s_rit.rit_beats)
                s_rit.rit_i++;

            if (s_rit.rit_i >= s_rit.rit_beats) {
                /* 설정된 박 수를 다 채우면 HOLD 또는 OFF로 이동 */
                s_rit.phase = (s_rit.hold_beats > 0)
                            ? _RIT_PHASE_HOLD
                            : _RIT_PHASE_OFF;
            }
        }

        /* ❗ 여기서는 더 이상 g_met_rit_mode(RIT/ACCEL)를 변경하지 않는다.
           - 플랜 시작 시점에 RIT/ACCEL로 세팅된 값을 유지
           - 플랜이 끝나거나 중간에 강제 종료될 때만 OFF로 바뀜 */
        return;
    }

    case _RIT_PHASE_HOLD: {
        if (s_rit.hold_beats) {
            s_rit.hold_i++;
            if (s_rit.hold_i >= s_rit.hold_beats)
                s_rit.phase = _RIT_PHASE_OFF;
        }

        /* HOLD 동안은 모드 표시를 끈 상태로 유지 */
        g_met_rit_mode = MET_RIT_MODE_OFF;
        return;
    }
    }
}


/* 10Hz로 부드럽게 BPM을 갱신: rit_i(정수 박) + g_met_phi_q16(박 내 위상) 기반 */
static inline void Metronome_RitAdvance_Continuous_10Hz(void)
{
    static uint32_t s_next_ms = 0;
    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - s_next_ms) < 0) return;
    s_next_ms = now + 100; // 10Hz

    switch (s_rit.phase) {
        default:
        case _RIT_PHASE_OFF:
            return;

        case _RIT_PHASE_RIT: {
            if (s_rit.rit_beats == 0) {
                MetronomeBPM = s_rit.targ_bpm;
                return;
            }
            /* u = (rit_i + phi) / rit_beats  → Q15 */
            uint64_t num_q16 = ((uint64_t)s_rit.rit_i << 16) + (uint64_t)g_met_phi_q16; // 0..(rit_beats<<16)
            uint64_t den_q16 = ((uint64_t)s_rit.rit_beats << 16);
            uint32_t u_q15   = (uint32_t)((num_q16 * 32768u) / (den_q16 ? den_q16 : 1)); // 0..32768

            uint16_t f = _ease_apply((uint16_t)u_q15, s_rit.curve_rit);
            uint16_t bpm = _q15_lerp_u16(s_rit.base_bpm, s_rit.targ_bpm, f);
            if (bpm < MET_MIN_BPM) bpm = MET_MIN_BPM;
            if (bpm > MET_MAX_BPM) bpm = MET_MAX_BPM;
            MetronomeBPM = bpm;
            return;
        }

        case _RIT_PHASE_HOLD:
            MetronomeBPM = s_rit.targ_bpm; // 홀드 동안 고정
            return;
    }
}


void Metronome_ResetBeatCounter(void)
{
    g_met_big_cnt   = 1;
    g_met_small_cnt = 1;
    Met_DrawBeatField(g_met_big_cnt, g_met_small_cnt, 0);  // ❗ fixed_zeros = 0
}


void Met_DrawBeatField(uint32_t big, uint8_t small, uint8_t fixed_zeros)
{
    uint8_t ts = TimeSignature;
    uint8_t use_wide = (ts >= 10);     // 2자리 타임시그면 6칸 포맷

    // 대기 화면용 표시
    if (fixed_zeros) {
        if (use_wide) {
            // 6칸 포맷: ---:--
            LCD16X2_Set_Cursor(MyLCD, 1, 11);
            LCD16X2_Write_String(MyLCD, "---:--");
        } else {
            // 5칸 포맷: ---:-
            LCD16X2_Set_Cursor(MyLCD, 1, 12);
            LCD16X2_Write_String(MyLCD, "---:-");
        }
        return;
    }

    // 실제 카운터 찍기
    if (use_wide) {
        // 6칸: [0][1][2][:][4][5]
        char field[7] = "      ";

        // 큰박 0~999까지 오른쪽 정렬
        uint32_t big3 = big % 1000;
        field[2] = (big3 % 10) + '0';
        big3 /= 10;
        field[1] = (big3 > 0) ? ((big3 % 10) + '0') : ' ';
        big3 /= 10;
        field[0] = (big3 > 0) ? ((big3 % 10) + '0') : ' ';

        field[3] = ':';   // 여기가 신성불가침 자리

        // 작은박 0~99 → 공백 + 1자리  or  2자리
        if (small < 10) {
            field[4] = ' ';
            field[5] = '0' + small;
        } else {
            small %= 100;
            field[4] = '0' + (small / 10);
            field[5] = '0' + (small % 10);
        }

        LCD16X2_Set_Cursor(MyLCD, 1, 11);
        LCD16X2_Write_String(MyLCD, field);
    } else {
        // 5칸: [0][1][2][:][4]
        // TS<10 이면 small도 최대 9까지밖에 안 올라감 → 1자리로 고정 가능
        char field[6] = "     ";

        uint32_t big3 = big % 1000;
        field[2] = (big3 % 10) + '0';
        big3 /= 10;
        field[1] = (big3 > 0) ? ((big3 % 10) + '0') : ' ';
        big3 /= 10;
        field[0] = (big3 > 0) ? ((big3 % 10) + '0') : ' ';

        field[3] = ':';              // 이것도 신성불가침
        field[4] = '0' + (small % 10);

        LCD16X2_Set_Cursor(MyLCD, 1, 12);
        LCD16X2_Write_String(MyLCD, field);
    }
}

// 메트로놈 사운드/플래시 (하나의 함수, TIM2 설정값 변경 금지)
// - TIM2는 MX_TIM2_Init()로 이미 PSC=72-1, ARR=0xFFFFFFFF로 설정되어 있다고 가정
// - 여기서는 TIM2 설정을 건드리지 않음. 필요 시 '시작'만 한다(HAL_TIM_Base_Start).
// - IsMetronomeReady==1 이면 BPM/TS에 맞춰 비프 펄스(PB4=High, PB5=Low) & LCDColor 플래시(5 → 자동 2 복귀)
// - 딜레이 없음(타이머 기반), 지터 누적 방지(다음 박자 시각을 고정 간격 증가)
// ────────────────────────────────────────────────────────────────


// 함수 맨 위 static 영역에 추가

void MetronomeSound(void)
{
    const uint32_t PULSE_HIGH_TICKS = 50000U;
    const uint32_t PULSE_LOW_TICKS  = 30000U;
    const uint32_t FLASH_TICKS      = 125000U;

    static uint32_t s_last_beat_us = 0;   // [NEW] 이번 박 시작 시각(us)
    static uint32_t s_cur_beat_us  = 1;   // [NEW] 이번 박 길이(us, 0 방지)


    extern TIM_HandleTypeDef htim2;

    // 내부 상태
    static uint8_t  s_started       = 0;
    static uint8_t  s_in_pulse      = 0;
    static uint8_t  s_beat_idx      = 0;
    static uint32_t s_next_beat     = 0;
    static uint32_t s_pulse_off     = 0;
    static uint32_t s_flash_off     = 0;
    static uint8_t  s_timer_started = 0;

    // 서브비트 상태(패턴용)
    static uint32_t s_next_sub      = 0;
    static uint8_t  s_sub_steps     = 1;
    static uint8_t  s_sub_mask      = 0x01;
    static uint8_t  s_sub_idx       = 0;

    // 분모 보정용
    static uint16_t s_prev_bpm      = 0;
    static uint8_t  s_prev_den      = 0;
    static uint8_t  s_den_erracc    = 0;   // 0..den-1

    // 재생 ON 에지 검출용
    static uint8_t  s_prevReady     = 0;

    #define DUE_OR_PAST(now,deadline) ((int32_t)((now)-(deadline)) >= 0)

    if (!s_timer_started) {
        (void)HAL_TIM_Base_Start(&htim2);
        s_timer_started = 1;
    }

    // 현재 재생 상태
    uint8_t ready = IsMetronomeReady;
    g_met_ready = ready;  // [NEW]

    // 꺼져있으면 리셋
    if (!ready) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
        if (s_flash_off) { LCDColorSet(2); s_flash_off = 0; }
        s_started    = 0;
        s_in_pulse   = 0;
        s_next_sub   = 0;
        s_prevReady  = 0;           // 다음에 켜질 때 에지 감지

        g_met_ready      = 0;   // [NEW]
        g_met_phi_q16    = 0;   // [NEW]
        g_met_beat_inbar = 0;   // [NEW]
        g_met_rit_mode   = MET_RIT_MODE_OFF;  // [NEW]
        //g_met_rit_base_bpm   = 0;           // [NEW]
        //g_met_rit_target_bpm = 0;           // [NEW]

        return;
    }

    // 🔥 방금 켜진 순간 (0 → 1)
    if (!s_prevReady && ready) {
        if (g_met_req_counter_reset) {
            g_met_req_counter_reset = 0;
            g_met_big_cnt   = 0;
            g_met_small_cnt = 1;
            Met_DrawBeatField(g_met_big_cnt, g_met_small_cnt, 0);  // 1:1 실표시
        }

        /* 재생 ON 순간, a tempo 앵커 래치 + 리타르단도 플랜 클리어 */
        g_met_atempo_anchor_bpm = MetronomeBPM;
        s_rit.base_bpm = MetronomeBPM;
        s_rit.phase    = _RIT_PHASE_OFF;
    }
    s_prevReady = ready;


    // ─── 현재 설정 읽기 ───
    uint16_t bpm = MetronomeBPM;
    if (bpm < 20)  bpm = 20;
    if (bpm > 300) bpm = 300;

    uint8_t ts = TimeSignature;
    if (ts < 1)  ts = 1;
    if (ts > 32) ts = 32;   // ★★ 여기! 예전엔 9로 잘라서 16/16도 9번만 갔던 거

    uint8_t den = TimeSignatureDen;
    if (den < 1)  den = 1;
    if (den > 32) den = 32;

    // ─── 분모까지 반영한 박 길이 ───
    uint32_t quarter_us   = (60000000UL + bpm/2U) / bpm;  // 4분음표 길이
    uint32_t base_us      = quarter_us * 4UL;             // 공통 분자
    uint32_t beat_interval = base_us / den;               // 주 박 길이
    uint8_t  beat_rem      = (uint8_t)(base_us % den);    // 나머지

    // bpm/den 바뀌었으면 누적 리셋
    if (bpm != s_prev_bpm || den != s_prev_den) {
        s_den_erracc = 0;
        s_prev_bpm   = bpm;
        s_prev_den   = den;
    }

    // 현재 시각
    uint32_t now = __HAL_TIM_GET_COUNTER(&htim2);

    // [NEW] 공유 위상 갱신 (0..65535)
    {
        uint32_t dt = (now >= s_last_beat_us) ? (now - s_last_beat_us) : 0u;
        uint32_t denom = s_cur_beat_us ? s_cur_beat_us : 1u;
        if (dt > denom) dt = denom;
        g_met_phi_q16 = (uint32_t)(((uint64_t)dt << 16) / denom);
    }

    /* [INSERT just after phase update] */
    /* A TEMPO 즉시 반영: 요청되면 다음 박을 '지금'으로 당겨서 atempo로 시작 */
    if (g_met_atempo_force_nextbeat) {
        g_met_atempo_force_nextbeat = 0;

        /* beat_interval은 직전 계산된 현 BPM 기준 */
        s_last_beat_us = now;
        s_cur_beat_us  = beat_interval;
        g_met_phi_q16  = 0;

        /* 다음 박 예정 시각을 지금으로 당김 → 즉시 다음 박으로 넘어감 */
        s_next_beat = now;
    }


    /* ★ 여기 바로 아래에 추가: 박 중간에도 10Hz로 부드럽게 BPM 갱신 */
    Metronome_RitAdvance_Continuous_10Hz();

    // LCD 플래시 복귀
    if (s_flash_off && DUE_OR_PAST(now, s_flash_off)) {
        LCDColorSet(2);
        s_flash_off = 0;
    }

    // 펄스 끝
    if (s_in_pulse && DUE_OR_PAST(now, s_pulse_off)) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
        s_in_pulse = 0;
    }

    // 첫 진입
    if (!s_started) {
        s_started   = 1;
        s_in_pulse  = 0;
        s_beat_idx  = 0;
        s_next_beat = now;

        s_last_beat_us   = now;            // [NEW]
        s_cur_beat_us    = beat_interval;  // [NEW]
        g_met_phi_q16    = 0;              // [NEW]
        g_met_beat_inbar = 0;              // [NEW]


        // 첫 박 서브비트 세팅
        s_sub_steps = g_metSubSteps;
        s_sub_mask  = g_metSubMask;
        if (s_sub_steps > 1) {
            uint32_t sub_int = beat_interval / s_sub_steps;
            s_sub_idx  = 1;
            s_next_sub = now + sub_int;
        } else {
            s_sub_idx  = 0;
            s_next_sub = 0;
        }

        LCDColorSet(2);
    }

    // ─── 메인 박 타이밍 ───
    now = __HAL_TIM_GET_COUNTER(&htim2);
    if (!s_in_pulse && DUE_OR_PAST(now, s_next_beat)) {

        g_met_beat_inbar = s_beat_idx;     // [NEW] 현재 박 인덱스
        s_last_beat_us   = now;            // [NEW] 이번 박 시작 시각
        s_cur_beat_us    = beat_interval;  // [NEW] 이번 박 길이
        g_met_phi_q16    = 0;              // [NEW] 위상 0부터 시작


        uint8_t use_high = (ts == 1) ? 1 : (s_beat_idx == 0);

        if (use_high) {
            // 강박
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
            LCDColorSet(3);
            s_pulse_off = now + PULSE_HIGH_TICKS;

            g_met_big_cnt++;
            g_met_small_cnt = 1;
            Met_DrawBeatField(g_met_big_cnt, g_met_small_cnt, 0);
            notch_metronome_click(1);
        } else {
            // 약박
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
            LCDColorSet(7);
            s_pulse_off = now + PULSE_LOW_TICKS;

            if (g_met_small_cnt == 0) g_met_small_cnt = 1;
            else g_met_small_cnt++;
            Met_DrawBeatField(g_met_big_cnt, g_met_small_cnt, 0);
            notch_metronome_click(0);
        }

        s_in_pulse  = 1;
        s_flash_off = now + FLASH_TICKS;

        // 다음 박 예약 + 나머지 보정
        s_next_beat += beat_interval;
        s_den_erracc += beat_rem;
        if (s_den_erracc >= den) {
            s_next_beat += 1;     // 1µs 보정
            s_den_erracc -= den;
        }

        // 마디 인덱스
        s_beat_idx++;
        if (s_beat_idx >= ts) s_beat_idx = 0;

        // 이 박에서 쓸 패턴 복사
        s_sub_steps = g_metSubSteps;
        s_sub_mask  = g_metSubMask;
        if (s_sub_steps > 1) {
            uint32_t sub_int = beat_interval / s_sub_steps;
            s_sub_idx  = 1;
            s_next_sub = (s_next_beat - beat_interval) + sub_int;
        } else {
            s_sub_idx  = 0;
            s_next_sub = 0;
        }

        Metronome_RitOnBeatTick();

    }

    // ─── 서브비트 처리 ───
    now = __HAL_TIM_GET_COUNTER(&htim2);
    if (s_sub_steps > 1 && s_next_sub && DUE_OR_PAST(now, s_next_sub)) {
        if (s_sub_mask & (1u << s_sub_idx)) {
            notch_metronome_subclick();
        }

        s_sub_idx++;
        if (s_sub_idx < s_sub_steps) {
            uint32_t sub_int = beat_interval / s_sub_steps;
            s_next_sub += sub_int;
        } else {
            s_next_sub = 0;
        }
    }
}



void DSPModeSelect(DSPMode currentDSPmode){ /// BINARY INPUT을 받습니다.

	switch (currentDSPmode){

		case DSP_Mode_Standby: // DSP Standby - Stereo Input Turned Off / Mono Mic Passthrough Only
			currentDSPModeState = DSP_Standby;
    		break;
		case DSP_Mode_Practice: // DSP Practice Mode
			currentDSPModeState = DSP_AudioLoopBack;
    		break;
		case DSP_Mode_Tuner: // DSP Tuner Mode (Send Tuner Value Via UART)
			currentDSPModeState = DSP_Tuner;
    		break;
		case DSP_Mode_Metronome: // DSP Metronome Mode (411 Sends Metronome value to DSP)
			currentDSPModeState = DSP_Metronome;
    		break;
		case DSP_Mode_SoundGen: // DSP Sound Generator Mode
			currentDSPModeState = DSP_SoundGenerator;
    		break;
		case DSP_Mode_FirmwareUpdate:
			currentDSPModeState = DSP_TestMode1;
    		break;
		case DSP_Mode_Reserved_1:

    		break;
		case DSP_Mode_Reserved_2:

    		break;
		default:


	}

}





void RenderUI(void) {

    switch (currentUIState) {

        // 🌟 Welcome & Mode Select
        case UI_WELCOME_SCREEN:
        	DSPModeSelect(DSP_Mode_Standby);
        	WelcomeScreen();
            break;

        case UI_MODE_SELECTION:
        	DSPModeSelect(DSP_Mode_Standby);
        	ModeSelection();
        	LEDModeSet(1);

            break;


        // 🎸 Practice Group
        case UI_PRACTIVE_MODE_INTRO:
        	DSPModeSelect(DSP_Mode_Standby);
        	PracticeIntro();

            break;

        case UI_PRACTICE_HOME:
        	LEDModeSet(8);
        	if (AudioProcessingIsReady == 1) {
        		DSPModeSelect(DSP_Mode_Practice);
        	} else {
        		DSPModeSelect(DSP_Mode_Standby);
        	}
        	PracticeHome();


            break;

        case UI_PRACTICE_FREQ_SETTING_CUTOFF_START:

        	if (AudioProcessingIsReady == 1) {
        		DSPModeSelect(DSP_Mode_Practice);
        	} else {
        		DSPModeSelect(DSP_Mode_Standby);
        	}
        	PracticeFreqSettingCutoffStart();


        	break;

        case UI_PRACTICE_FREQ_SETTING_CUTOFF_END:

        	if (AudioProcessingIsReady == 1) {
        		DSPModeSelect(DSP_Mode_Practice);
        	} else {
        		DSPModeSelect(DSP_Mode_Standby);
        	}
        	PracticeFreqSettingCutoffEnd();


            break;

        case UI_PRACTICE_INST_PRESET_SETTING:

        	if (AudioProcessingIsReady == 1) {
        		DSPModeSelect(DSP_Mode_Practice);
        	} else {
        		DSPModeSelect(DSP_Mode_Standby);
        	}
        	PracticeInstPresetSetting();

            break;







        // 🔧 Tuner
        case UI_TUNER_MODE_INTRO:

        	DSPModeSelect(DSP_Mode_Standby);
        	TunerIntro();
            break;

        case UI_TUNER_HOME:

        	DSPModeSelect(DSP_Mode_Tuner);
        	TunerHome();
        	LEDModeSet(6);

            break;

        case UI_TUNER_BASE_A_FREQ_SETTING:

        	DSPModeSelect(DSP_Mode_Standby);
        	TunerBaseAFreqSetting();
        	LEDModeSet(0);
            break;






        // 🕐 Metronome
        case UI_METRONOME_MODE_INTRO:

        	DSPModeSelect(DSP_Mode_Standby);
        	MetronomeModeIntro();
        	LEDModeSet(0);
            break;

        case UI_METRONOME_HOME:

        	if (IsMetronomeReady == 1) {
        		DSPModeSelect(DSP_Mode_Metronome);
        	} else {
        		DSPModeSelect(DSP_Mode_Standby);
        	}
        	MetronomeHome();
        	MetronomeSound();

            {
                 static uint32_t s_tot_last_ms = 0;
                 uint32_t __now = HAL_GetTick();
                 uint32_t __dt  = (s_tot_last_ms == 0) ? 0 : (__now - s_tot_last_ms);
                 s_tot_last_ms  = __now;

                 g_sw_total_ms += __dt;

                 uint32_t __hrs  = g_sw_total_ms / 3600000UL;
                 uint32_t __mins = (g_sw_total_ms % 3600000UL) / 60000UL;
                 if (__hrs > 9999UL || (__hrs == 9999UL && __mins > 59UL)) {
                     g_sw_total_ms = 0;
                 }

                 {
                     uint32_t __hrsS  = g_sw_session_ms / 3600000UL;
                     uint32_t __minsS = (g_sw_session_ms % 3600000UL) / 60000UL;
                     g_sw_session_ms += __dt;
                     if (__hrsS > 999UL || (__hrsS == 999UL && __minsS > 59UL)) g_sw_session_ms = 0;
                 }
             }

            // ★ 세션도 TOTAL과 함께 증가
            LEDModeSet(4);

            break;

        case UI_METRONOME_BPM_SETTING:

        	if (IsMetronomeReady == 1) {
        		DSPModeSelect(DSP_Mode_Metronome);
        	} else {
        		DSPModeSelect(DSP_Mode_Standby);
        	}
        	MetronomeBPMSetting();
        	LEDModeSet(0);
            break;

        case UI_METRONOME_TIME_SIGNATURE_SETTING:

        	if (IsMetronomeReady == 1) {
        		DSPModeSelect(DSP_Mode_Metronome);
        	} else {
        		DSPModeSelect(DSP_Mode_Standby);
        	}
        	MetronomeTimeSignatureSetting();
        	LEDModeSet(0);
            break;
        case UI_METRONOME_TIMING_CALCULATION:

        	DSPModeSelect(DSP_Mode_Standby);
        	MetronomeTimingCalc();

            break;







        // 🎹 SoundGen
        case UI_SOUNDGEN_INTRO:

        	DSPModeSelect(DSP_Mode_Standby);
        	SoundGenIntro();
        	break;

        case UI_SOUNDGEN_HOME:

           	if (IsSoundGenReady == 1) {
            		DSPModeSelect(DSP_Mode_SoundGen);
            	} else {
            		DSPModeSelect(DSP_Mode_Standby);
            	}
           	SoundGenHome();
            LEDModeSet(5);
            break;

        case UI_SOUNDGEN_BASE_A_FREQ_SETTING:

           	if (IsSoundGenReady == 1) {
            		DSPModeSelect(DSP_Mode_SoundGen);
            	} else {
            		DSPModeSelect(DSP_Mode_Standby);
            	}
           	SoundGenBaseAFreqSetting();
           	LEDModeSet(0);
            break;

        // ⚙️ Settings
        case UI_SETTINGS_HOME:

        	DSPModeSelect(DSP_Mode_Standby);
        	SettingsHome();
        	LEDModeSet(0);
            break;

        case UI_SETTINGS_METRONOME_LENGTH:
        	SettingsHelp();
        	LEDModeSet(0);

            break;


        case UI_SETTINGS_POWEROFF_CONFIRM:
        	SettingsPowerOffConfirm();
        	LEDModeSet(0);

            break;

        case UI_SETTINGS_ABOUT:
        	SettingsAbout();
        	LEDModeSet(0);
            break;

        case UI_SETTINGS_FW_UPDATE:
        	SettingsFWUpdate();
        	LEDModeSet(0);
            break;






        // 🔊 Volume & Balance
        case UI_MASTER_VOLUME:
        	if (IsMetronomeReady == 1) { MetronomeSound(); }  // [ADD]
        	VolumeControl();
        	LEDModeSet(2);
            break;

        case UI_SOUND_BALANCE:
        	if (IsMetronomeReady == 1) { MetronomeSound(); }  // [ADD]
        	BalanceControl();
        	LEDModeSet(3);

            break;

        case UI_METRONOME_VOLUME:
        	if (IsMetronomeReady == 1) { MetronomeSound(); }  // [ADD]
            MetronomeVolumeControl();   // 아래 (4)에서 새로 추가하는 함수
            LEDModeSet(7);
            break;






        // ⛔️ Unknown fallback
        default:
        	CurrentErrorStatus = 1;
            break;
    }
}


uint16_t ErrorHandling(void){

}

void ErrorDisplay(void){

	switch(CurrentErrorStatus){
		case 0:
			break;
		case 1:
			LCD16X2_Clear(MyLCD);
        	LCDColorSet(5);
            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            LCD16X2_Write_String(MyLCD, "Error 0x01");
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, "UNK FALLBACK!");
            HAL_Delay(5000);
            LCD16X2_ScrollTextDelay(MyLCD,
                "Unknown Fallback Occured. Please Restart Device! Sorry for inconvenience :(",
                200, 0, 2, 1);
			break;
		case 2:
			LCD16X2_Clear(MyLCD);
        	LCDColorSet(5);
            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            LCD16X2_Write_String(MyLCD, "Error 0x02");
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, "DSP COMM FAIL!");
            HAL_Delay(5000);
            LCD16X2_ScrollTextDelay(MyLCD,
                "DSP Not Responding! Please Restart Device! Sorry for inconvenience :(",
                200, 0, 2, 1);
			break;
		case 3:
			LCD16X2_Clear(MyLCD);
        	LCDColorSet(5);
            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            LCD16X2_Write_String(MyLCD, "Error 0x03");
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, "DSP RETURNED ERR");
            HAL_Delay(5000);
            LCD16X2_ScrollTextDelay(MyLCD,
                "DSP Returned An Error! Please Restart Device! Sorry for inconvenience :(",
                200, 0, 2, 1);
			break;
		case 4:
			LCD16X2_Clear(MyLCD);
        	LCDColorSet(5);
            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            LCD16X2_Write_String(MyLCD, "Error 0x04");
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, "DATA INTEGRITY! ");
            HAL_Delay(5000);
            LCD16X2_ScrollTextDelay(MyLCD,
                "Data integrity check failure! Sorry for inconvenience :(",
                200, 0, 2, 1);
			break;
		default:
			break;

	}

}


void WelcomeScreen(void) {
    static uint8_t firstRun = 0;
    if (!firstRun) {
    	/*
        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "Welcome!");
        LCD16X2_ScrollTextDelay(MyLCD,
            "Hello Maestro! Preparing System...",
            200, 0, 2, 1);
            */
        Welcomeeventisdone = 1;
        firstRun = 1;
    }
    if (Welcomeeventisdone) {
        currentUIState = UI_MODE_SELECTION;
    }
}


//========= VU METER (APP) ================
// LCD에 VU를 2..16 열에 수평 바 형태로 그림
// 세그값(seg=0..30), 2세그=1칸, 15칸(=2..16) 사용
static void VU_DrawRow(uint8_t row, uint8_t segs)
{
    // 열 2..16 → 칸 index 0..14
    for (uint8_t col = 2; col <= 16; col++) {
        uint8_t cell_idx   = (col - 2);        // 0..14
        uint8_t cell_base  = cell_idx * 2;     // 이 칸의 첫 세그 인덱스
        // 이 칸에 들어갈 상태 결정
        uint8_t ch; // 0=space, 6=Half, 7=Full  (아래 CreateChar에서 6,7 할당)
        if (segs > (cell_base + 1)) {
            ch = 7; // Full
        } else if (segs == (cell_base + 1)) {
            ch = 6; // Half
        } else if (segs == cell_base) {
            ch = 6; // "3→ 2번칸 Full + 3번칸 Half" 요구를 맞추려면 여기선 Half가 아님!
                   // ※ 요구사항: 값=3이면 2번칸 Full + 3번칸 Half
                   // 위 공식을 만족하려면 segs==cell_base일 땐 빈칸이어야 한다.
                   // 따라서 바로 아래처럼 공백으로 처리한다.
            ch = 255; // will map to space
        } else {
            ch = 255; // space
        }

        if (ch == 255) {
            LCD16X2_Set_Cursor(MyLCD, row, col);
            LCD16X2_Write_Char(MyLCD, ' ');
        } else {
            LCD16X2_DisplayCustomChar(0, row, col, ch); // (0,row,col,idx) 패턴은 기존과 동일. :contentReference[oaicite:4]{index=4}
        }
    }
}

void Practice_VUMeter_App(void)
{
    uint32_t now = HAL_GetTick();
    if (!g_vu_inited) {
        // 1) 기존 커스텀 문자 전부 정리 후, VU 전용 슬롯(6,7)만 등록
        LCD16X2_ClearCustomChars(0);                    // ★ 리셋
        LCD16X2_RegisterCustomChar(0, 4, Lmini); // ★ 재등록
        LCD16X2_RegisterCustomChar(0, 5, Rmini);
        LCD16X2_RegisterCustomChar(0, 6, HalfFilledVU); // ★ 재등록
        LCD16X2_RegisterCustomChar(0, 7, FullFilledVU);

        // 2) 현재 백라이트 색 저장(탈출 시 복귀)
        g_vu_prev_color = g_lcd_last_color;

        // 3) 화면 초기화
        LCD16X2_Clear(MyLCD);
        LCD16X2_DisplayCustomChar(0, 1, 1, 4);
        LCD16X2_DisplayCustomChar(0, 2, 1, 5);

        g_vu_inited  = 1;
        g_vu_next_ms = HAL_GetTick();
    }


    if ((int32_t)(now - g_vu_next_ms) < 0) return;
    g_vu_next_ms = now + VU_REFRESH_MS;

    uint8_t segL=0, segR=0;
    notch_get_vu_segments(&segL, &segR);

    //// 피크를 넘어서면 색깔을 점등합니다.
    uint8_t peakSeg = (segL > segR) ? segL : segR;
    if (peakSeg >= VU_RED_SEG)       LCDColorSet(5); // 빨강
    else if (peakSeg >= VU_YELLOW_SEG) LCDColorSet(3); // 초록
    else                              LCDColorSet(2); // 스카이



    VU_DrawRow(1, segL);
    VU_DrawRow(2, segR);
}




///////////////////////



// ===== Mode line (row2) helpers =====
// 프레임 s(0..steps-1)에 대한 ease-in-out 지연(ms)을 계산
static inline uint32_t Anim_EaseDelay(uint32_t base_ms, int step, int steps) {
    if (steps < 2) return base_ms;

    // base_ms 기준으로 [min..max] 범위에서 가변 (느림→빠름→느림)
    uint32_t min_ms = (base_ms >= 2) ? (base_ms / 2) : 1;     // 중간 지점의 최소 지연
    uint32_t max_ms = base_ms * 2 + 1;                        // 시작/끝 지점의 최대 지연
    if (max_ms <= min_ms) max_ms = min_ms + 1;

    // w = 4 * (t - 0.5)^2  (t = step/(steps-1))  → 0(중앙) ~ 1(양끝)
    int32_t s2 = 2*step - (steps - 1);                        // [-N..+N]
    int64_t num = 4LL * s2 * s2;                              // 4*s^2
    int64_t den = (int64_t)(steps - 1) * (steps - 1);         // (steps-1)^2

    uint32_t span = (uint32_t)(max_ms - min_ms);
    uint32_t w_ms = (uint32_t)((num * span + den/2) / den);   // 반올림
    return min_ms + w_ms;                                      // 양끝 느림, 중앙 빠름
}

// 0..steps 를 0..1024(Q10)로 easeInOutCubic 보간값으로 변환
static inline int32_t EaseInOutCubic_Q10(int step, int steps) {
    if (steps <= 0) return 1024;
    // t ∈ [0,1024]
    int32_t t = (int32_t)((step * 1024LL + steps/2) / steps);
    if (t < 512) {
        // 4t^3, with t∈[0,0.5] → Q10: 4*(t/1024)^3
        int64_t x = t;                          // Q10
        return (int32_t)((4LL * x * x * x + (1<<19)) >> 20); // ≈ round(4*x^3 / 2^20)
    } else {
        // 1 - ((-2t+2)^3)/2
        int32_t u = 2048 - (t<<1);              // u = -2t + 2 (Q10)
        int64_t x = u;                           // Q10
        int32_t half = (int32_t)((x * x * x + (1<<20)) >> 21); // (u^3)/2 in Q10
        return 1024 - half;                      // 1 - (u^3)/2
    }
}

// 윈도우(열 2..15, 폭 14)에 두 라벨을 '겹쳐' 그리기 (겹치면 새 라벨이 우선)
static inline void ModeLine2_RenderBlend(const char* oldS, int lf, int posOld,
                                         const char* newS, int ln, int posNew) {
    const int W = 14;
    char win[W+1]; for (int i=0;i<W;i++) win[i] = ' ';

    // OLD 배치
    for (int i=0;i<lf;i++) {
        int col = posOld + i;
        if (0 <= col && col < W) win[col] = oldS[i];
    }
    // NEW 배치(우선순위 높음)
    for (int i=0;i<ln;i++) {
        int col = posNew + i;
        if (0 <= col && col < W) win[col] = newS[i];
    }
    win[W] = '\0';
    LCD16X2_Set_Cursor(MyLCD, 2, 2);
    LCD16X2_Write_String(MyLCD, win);
}





// 2열에 표시할 모드 라벨들 (Idle 상태에선 중앙 정렬)
static const char* kModeLabels[5] = {
    "PRACTICE", "TUNER", "METRONOME", "FREQ GEN", "SETTINGS"
};

// 2열의 양 끝 화살표(<, >) 그리기 규칙:
// - 현재 모드가 0(PRACTICE)  : 2,16에 '>' 만 표시 (2,1은 공백)
// - 현재 모드가 4(SETTINGS) : 2,1에 '<' 만 표시 (2,16은 공백)
// - 그 외(1~3)               : 2,1에 '<', 2,16에 '>' 표시
static void ModeLine2_DrawArrows(uint8_t mode)
{
    LCD16X2_Set_Cursor(MyLCD, 2, 1);
    if (mode == 0) LCD16X2_Write_String(MyLCD, " "); else LCD16X2_Write_String(MyLCD, "<");

    LCD16X2_Set_Cursor(MyLCD, 2, 16);
    if (mode == 4) LCD16X2_Write_String(MyLCD, " "); else LCD16X2_Write_String(MyLCD, ">");
}

// 2열 중앙(열 2..15, 폭 14칸)에 문자열을 '중앙 정렬'로 출력
static void ModeLine2_DrawCentered(const char* s)
{
    const int W = 14; // usable width: col 2..15
    int len = 0; while (s[len] && len < 32) len++;
    if (len > W) len = W;

    char line[W+1]; for (int i=0;i<W;i++) line[i] = ' ';
    int left = (W - len) / 2;
    for (int i=0;i<len;i++) line[left + i] = s[i];
    line[W] = '\0';

    LCD16X2_Set_Cursor(MyLCD, 2, 2); // start at col 2
    LCD16X2_Write_String(MyLCD, line); // always overwrite 14 chars
}

// (오른→왼, 혹은 왼→오) 한 틱 애니메이션으로 'from→to' 전환
// dir_cw = 1 이면 CW(오른쪽으로 돌림) → 텍스트가 오른쪽에서 왼쪽으로 흐름
// dir_cw = 0 이면 CCW(왼쪽으로 돌림) → 텍스트가 왼쪽에서 오른쪽으로 흐름
static void ModeLine2_AnimateChange(const char* from, const char* to, uint8_t to_mode,
                                    uint16_t step_delay_ms, uint8_t dir_cw)
{
    g_mode_anim_busy = 1;

    const int W = 14;

    int lf=0, lt=0; while (from[lf] && lf<32) lf++; while (to[lt] && lt<32) lt++;
    if (lf > W) lf = W; if (lt > W) lt = W;

    ModeLine2_DrawArrows(to_mode);

    // 중앙 정렬 기준 위치(윈도우 좌표, 0..W-1)
    const int base_from = (W - lf)/2;
    const int base_to   = (W - lt)/2;

    // 시작/끝 위치 설정 (CW: 오른→왼, CCW: 왼→오)
    int startOld, endOld, startNew, endNew;
    if (dir_cw) {
        // 이전: 중앙 → 왼쪽 바깥,  다음: 오른쪽 바깥 → 중앙
        startOld = base_from;  endOld = -lf;
        startNew = W;          endNew = base_to;
    } else {
        // 이전: 중앙 → 오른쪽 바깥, 다음: 왼쪽 바깥 → 중앙
        startOld = base_from;  endOld = W;
        startNew = -lt;        endNew = base_to;
    }

    // 총 프레임 수(거리 기준로 산정: 자연스러운 길이 확보)
    int distOld = (endOld > startOld) ? (endOld - startOld) : (startOld - endOld);
    int distNew = (endNew > startNew) ? (endNew - startNew) : (startNew - endNew);
    int steps   = distOld; if (distNew > steps) steps = distNew;
    if (steps < 8) steps = 8;            // 너무 짧으면 어색 → 최소 프레임
    if (steps > 40) steps = 40;          // 너무 길면 답답 → 최대 프레임(취향 조절)

    for (int s = 0; s <= steps; ++s) {
        int32_t w = EaseInOutCubic_Q10(s, steps);   // 0..1024
        // pos = round( start + (end-start) * w )
        int posOld = startOld + (int)(( (int64_t)(endOld - startOld) * w + 512) >> 10);
        int posNew = startNew + (int)(( (int64_t)(endNew - startNew) * w + 512) >> 10);

        ModeLine2_RenderBlend(from, lf, posOld, to, lt, posNew);
        HAL_Delay(step_delay_ms);  // 일정 프레임 간격, 속도 변화는 '위치'에 반영
    }

    // 최종 중앙 프레임으로 스냅(잔상 방지)
    ModeLine2_DrawCentered(to);

    // 입력 버퍼 폐기 & 락 해제(기존 유지)
    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;
    g_mode_anim_busy = 0;
}

static inline void DrawModeIcons(uint8_t selected, uint8_t blink)
{
    // 메뉴 글자 코드 매핑(이미 위에서 RegisterCustomChar로 2~6이 등록됨)
    const uint8_t kMenuGlyph[5] = {2,3,4,5,6}; // 12,13,14,15,16열용

    if (selected > 4) selected = 4;

    for (uint8_t i = 0; i < 5; ++i) {
        uint8_t glyph = kMenuGlyph[i];
        if (blink && i == selected) glyph = 1; // 하트(1)로 오버레이
        LCD16X2_DisplayCustomChar(0, 1, 12 + i, glyph);
    }
}



// 안전 클램프 & 라벨 접근 헬퍼 (애니메이션 코드는 기존 유지)
static inline uint8_t clamp_mode_idx(uint8_t m) {
    return (m < 5) ? m : (m % 5);
}
static inline const char* safe_mode_label(uint8_t m) {
    extern const char* kModeLabels[5];
    return kModeLabels[clamp_mode_idx(m)];
}


void ModeSelection(void) {
    LCDColorSet(6);


    // 깜빡임 변화 감지용 (기존 변수 유지)
    static uint8_t s_prevBlink = 0xFF;

    // ── 첫 진입 초기화 ─────────────────────────────────────────────
    if (!ModeSelectionFirstRunFlag) {

        g_dht_req = 1;
        Measure_Service();

    	IsSoundGenReady = 0;

        notch_set_tuner_enabled(0);
        BatteryTemp_HeaderMarkDirty();

        LCDColorSet(6);
        LCD16X2_Clear(MyLCD);
        LCD16X2_ClearCustomChars(0);

        // 커스텀 글자 등록 (기존과 동일)
        LCD16X2_RegisterCustomChar(0, 1, heartChar);
        LCD16X2_RegisterCustomChar(0, 2, MenuFirst);
        LCD16X2_RegisterCustomChar(0, 3, MenuSecond);
        LCD16X2_RegisterCustomChar(0, 4, MenuThird);
        LCD16X2_RegisterCustomChar(0, 5, MenuFourth);
        LCD16X2_RegisterCustomChar(0, 6, MenuFifth);

        /*
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "MAIN MENU");
        */



        // 현재 모드 인덱스 안전화
        CurrentModeStatus = clamp_mode_idx(CurrentModeStatus);

        // 상단 아이콘 & 2행 초기 텍스트 (안전 라벨 사용)
        DrawModeIcons(CurrentModeStatus, UITotalBlinkStatus);
        s_prevBlink = UITotalBlinkStatus;

        ModeLine2_DrawArrows(CurrentModeStatus);
        ModeLine2_DrawCentered(safe_mode_label(CurrentModeStatus)); // ★ 핵심: 안전라벨

        HAL_Delay(2);

        BatteryTemp_HeaderMarkDirty();
        ModeSelectionFirstRunFlag = 1;
        ModeSelectionPrevDisplay  = 255;
    }

    // 애니 도중 입력 무시 (기존 유지)
    if (g_mode_anim_busy) {
        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;
        return;
    }

    BatteryTemp_HeaderService();
    BatteryTemp_HeaderMarkDirty();


    // 상단 아이콘 깜빡임만 갱신 (텍스트는 깜빡이지 않음)
    if (s_prevBlink != UITotalBlinkStatus) {
        DrawModeIcons(clamp_mode_idx(CurrentModeStatus), UITotalBlinkStatus);
        s_prevBlink = UITotalBlinkStatus;
    }

    // ── 로터리 처리 ───────────────────────────────────────────────
    const uint16_t kStepDelay = 35;

    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        uint8_t from = clamp_mode_idx(CurrentModeStatus);
        uint8_t to   = clamp_mode_idx((uint8_t)(from + 1));

        rotaryEvent3 = ROTARY_EVENT_NONE;

        CurrentModeStatus = to; // 상태 갱신은 항상 클램프 결과로
        DrawModeIcons(CurrentModeStatus, UITotalBlinkStatus);

        // ★ 애니메이션은 기존 알고리즘 그대로, 단 라벨은 안전 포인터 사용
        ModeLine2_AnimateChange(
            safe_mode_label(from), safe_mode_label(to),
            to, kStepDelay, 1
        );

        ModeSelectionPrevDisplay = CurrentModeStatus;
        notch_ui_rotary_click_freq(1500.0f);
        return;
    }
    else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        uint8_t from = clamp_mode_idx(CurrentModeStatus);
        uint8_t to   = (from == 0) ? 4 : (uint8_t)(from - 1);
        to = clamp_mode_idx(to);

        rotaryEvent3 = ROTARY_EVENT_NONE;

        CurrentModeStatus = to;
        DrawModeIcons(CurrentModeStatus, UITotalBlinkStatus);

        ModeLine2_AnimateChange(
            safe_mode_label(from), safe_mode_label(to),
            to, kStepDelay, 0
        );

        ModeSelectionPrevDisplay = CurrentModeStatus;
        notch_ui_rotary_click_freq(1500.0f);
        return;
    }

    // ── 짧은 누름: 모드 진입 ───────────────────────────────────────
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;

        // 다음 화면으로 넘어갈 때도 인덱스는 안전화된 값만 사용
        uint8_t m = clamp_mode_idx(CurrentModeStatus);

        ModeSelectionFirstRunFlag = 0;
        LCD16X2_Clear(MyLCD);

        switch (m) {
            case 0: currentUIState = UI_PRACTICE_HOME;   break;
            case 1: currentUIState = UI_TUNER_HOME;      break;
            case 2: currentUIState = UI_METRONOME_HOME;  break;
            case 3: currentUIState = UI_SOUNDGEN_HOME;   break;
            case 4: currentUIState = UI_SETTINGS_HOME;   break;
            default: currentUIState = UI_MODE_SELECTION;  break; // 이론상 도달X, 안전넷
        }
        // 입력 버퍼 비우기 (모드 전환 후 잔여 입력으로 재진입 방지)
        notch_ui_button_beep();
        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;
        return;
    }

    // ── 짧은 누름: 모드 진입 ───────────────────────────────────────
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        ///////////////

        //Battery_ShowPopupLCD();
        notch_ui_button_beep();


        ///////////////
        // 입력 버퍼 비우기 (모드 전환 후 잔여 입력으로 재진입 방지)
        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;
        return;
    }


    if (buttonEvents[1] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[1] = BUTTON_EVENT_NONE;

        notch_ui_button_beep();
        ConfigStorage_Service(1);

        return; // UI는 건드리지 않음 (오디오만 즉시 반영)
    }


    // 혹시라도 외부에서 CurrentModeStatus가 깨졌다면, 틱마다 복구
    CurrentModeStatus = clamp_mode_idx(CurrentModeStatus);
}



// 정상 작동하는 코드
void PracticeIntro(){

}

////=========== PRACTICE 관련
static uint8_t g_PracticeCutoff_PreserveLine2_OnNextEntry = 0;



// "글리프가 등록되기 전" 요청된 토스트를 한 번 보류
static volatile uint8_t  s_toastPending       = 0;
static volatile uint8_t  s_toastPendingMode   = 0;

// "PracticeHome 최초 진입"에서 글리프 등록이 끝났는지
static volatile uint8_t  s_practiceCharsReady = 0;

// 첫 진입 토스트를 한 번만 띄웠는지 여부
static volatile uint8_t s_practiceFirstEntryToastShown = 0;

// End Freq 설정에서 PRACTICE로 복귀 직후, Notch 토스트 1회만 스킵
static volatile uint8_t g_SkipNotchToastOnce = 0;


// 재생/일시정지 아이콘을 한곳에서만 갱신 (토스트 중에는 그리지 않음)
static inline void Practice_DrawPlayPauseIcon_IfAllowed(void) {
    if (s_modeToastActive) return; // ★ 토스트 중엔 아이콘 금지
    if (AudioProcessingIsReady == 1) {
        LCD16X2_DisplayCustomChar(0, 2, 16, 0);
    } else {
        LCD16X2_DisplayCustomChar(0, 2, 16, 1);
    }
}


// ★ 토스트 한 번 찍고 1.5s 유지 (비블로킹)
//  - 글리프/헤더 준비 전이면 pending 처리
//  - 시작 시 2,16(우측 아이콘 자리)을 '공백'으로 명시적 클리어
static void Practice_ShowModeToast(uint8_t mode)
{
	Practice_RegisterChars_ForMode(CutOffOnOff);

    // ★ End Freq → PRACTICE 복귀 직후, Notch(1) 토스트만 1회 억제
    if (mode == 1 && g_SkipNotchToastOnce) { g_SkipNotchToastOnce = 0; return; }

    if (!s_practiceCharsReady) {
        // 아직 LCD 글리프/헤더가 준비 안 됨 → 최초 렌더 후 1회 실행하도록 보류
        s_toastPending     = 1;
        s_toastPendingMode = mode;
        return;
    }

    // 2,16에 남아 있던 재생/일시정지 아이콘을 명시적으로 지움(공백)
   //LCD16X2_Set_Cursor(MyLCD, 2, 16);
   //LCD16X2_Write_String(MyLCD, " ");

    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "DSP MODE:        ");
    LCD16X2_Set_Cursor(MyLCD, 2, 2);
    LCD16X2_Write_String(MyLCD, " ");

    // 중앙 문구
    LCD16X2_Set_Cursor(MyLCD, 2, 3);
    if (mode == 0)      { LCDColorSet(2); LCD16X2_Write_String(MyLCD, "DSP OFF      "); LCD16X2_DisplayCustomChar(0, 2, 1, 3); }
    else if (mode == 1) { LCDColorSet(3);  LCD16X2_Write_String(MyLCD, "NOTCH CUTOFF "); LCD16X2_DisplayCustomChar(0, 2, 1, 4); }
    else                { LCDColorSet(1); LCD16X2_Write_String(MyLCD, "PITCH SHIFT  "); LCD16X2_DisplayCustomChar(0, 2, 1, 5); }

    /*
     * 포인트: 2,16 공백 쓰기가 아이콘 “숨김”을 보장해. 이전엔 “그리지 않음”만 있었고, 이미 찍혀 있던 픽셀이 남아서 “변화가 없음”처럼 보였던 거야
     */

    // 1.5초 타이머 arm (비블로킹)
    s_modeToastActive   = 1;
    s_modeToastDeadline = HAL_GetTick() + 1500U;

    // 토스트 도중 모든 입력 폐기(안전)
    rotaryEvent3 = ROTARY_EVENT_NONE;
    buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;
}


// ── [Mode1: 악기 이름/범위만 부분 갱신] ──────────────────────────────
// ── [Mode1: 악기 범위만 부분 갱신 — 최소 수정] ──────────────────────────
static void UpdateSecondLine_Mode1_InstrumentOnly(void)
{
    // 2,1 멜로디 아이콘 (Mode1에서 슬롯4가 MelodyIcon으로 등록되어 있어야 함)
    LCD16X2_DisplayCustomChar(0, 2, 1, 4);

    // 현재 범위 사용: 선택(프리셋/유저프리셋)에 따라 이미 CutOffFreqStart/End 값이 갱신돼 있음
    uint32_t s = CutOffFreqStart;
    uint32_t e = CutOffFreqEnd;
    if (s < 20) s = 20;
    if (e < s)  e = s;

    // 2행 3..13만 클리어(11칸) → 우측 화살표/기타는 유지
    LCD16X2_Set_Cursor(MyLCD, 2, 3);
    LCD16X2_Write_String(MyLCD, "           "); // 11 spaces (col 3..13)

    // "XXXX-XXXX" (최대 9글자)
    char buf[12];
    snprintf(buf, sizeof(buf), "%4u-%4u", (unsigned)s, (unsigned)e);

    // 숫자 및 "Hz" 출력 (2,3.. / 2,12..13)
    LCD16X2_Set_Cursor(MyLCD, 2, 3);
    LCD16X2_Write_String(MyLCD, buf);
    LCD16X2_Set_Cursor(MyLCD, 2, 12);
    LCD16X2_Write_String(MyLCD, "Hz");
}


// ── [Mode2: 피치 쉬프터 부분 갱신] ───────────────────────────────────
static void UpdateSecondLine_Mode2_PitchOnly(void)
{
    // 2,1 튜닝포크 + 좌/우 화살표만 갱신
    LCD16X2_DisplayCustomChar(0, 2, 1, 5); // TuningFork
    LCD16X2_Set_Cursor(MyLCD, 2, 6);  LCD16X2_Write_String(MyLCD, "<");
    LCD16X2_Set_Cursor(MyLCD, 2, 14); LCD16X2_Write_String(MyLCD, ">");

    // 중앙 표기(2,3~2,5)
    LCD16X2_Set_Cursor(MyLCD, 2, 3);
    LCD16X2_Write_String(MyLCD, "   ");
    switch (PitchSemitone) {
        case 3:  LCD16X2_Set_Cursor(MyLCD, 2, 4); LCD16X2_Write_String(MyLCD, "0"); break;
        case 0:  LCD16X2_DisplayCustomChar(0, 2, 3, 2); LCD16X2_Set_Cursor(MyLCD, 2, 4); LCD16X2_Write_String(MyLCD, "3"); break; // -3
        case 1:  LCD16X2_DisplayCustomChar(0, 2, 3, 2); LCD16X2_Set_Cursor(MyLCD, 2, 4); LCD16X2_Write_String(MyLCD, "2"); break; // -2
        case 2:  LCD16X2_DisplayCustomChar(0, 2, 3, 2); LCD16X2_Set_Cursor(MyLCD, 2, 4); LCD16X2_Write_String(MyLCD, "1"); break; // -1
        case 4:  LCD16X2_Set_Cursor(MyLCD, 2, 3); LCD16X2_Write_String(MyLCD, "#1"); break;
        case 5:  LCD16X2_Set_Cursor(MyLCD, 2, 3); LCD16X2_Write_String(MyLCD, "#2"); break;
        case 6:  LCD16X2_Set_Cursor(MyLCD, 2, 3); LCD16X2_Write_String(MyLCD, "#3"); break;
        default: LCD16X2_Set_Cursor(MyLCD, 2, 3); LCD16X2_Write_String(MyLCD, " 0"); break;
    }

    // 게이지(2,7~2,13): 선택칸만 MARK, 나머지 BAR
    for (int col = 7; col <= 13; ++col) {
        uint8_t glyph = (col == (7 + PitchSemitone)) ? PITCH_MARK_IDX : PITCH_BAR_IDX;
        LCD16X2_DisplayCustomChar(0, 2, col, glyph);
    }
}



















////////////////////////// TIMER ///////////////////////////////////
// =====[Practice: DIRECT OUT 타이머/스톱워치 메뉴 - STUB 구현]=====
#define LCD_ROW2_START_COL   3
#define LCD_ROW2_END_COL     14
#define LCD_ROW2_WIDTH       (LCD_ROW2_END_COL - LCD_ROW2_START_COL + 1)
#define CLOCK_ICON_IDX       4  // mode==0에서 등록한 커스텀 인덱스

static inline uint8_t TS_InPracticeDirect(void) {
    // PracticeHome 화면 + DIRECT OUT 모드에서만 UI 조작/표시
    return (currentUIState == UI_PRACTICE_HOME) && (CutOffOnOff == 0);
}

// H:MM (시간 앞자리 0 제거, 분은 2자리 제로패드). (2,8) 기준(out12[5]~[10])에 그린다.
// blink_colon !=0 이면 ':' 표시, 0이면 공백으로 깜빡임.
static void TS_Format_session_HH_MM_nolead(uint32_t ms, char* out12 /*>=13*/, uint8_t blink_colon) {
    for (int i = 0; i < 12; ++i) out12[i] = ' ';
    out12[12] = 0;

    uint32_t hrs  = ms / 3600000UL;                 // 0..(99 한계 처리 별도)
    uint32_t mins = (ms % 3600000UL) / 60000UL;     // 0..59

    // (2,8) 시작 → out12[5]~ 에 우측정렬
    if (hrs >= 100) { // 이 케이스는 정상적으로는 오지 않지만 안전망
        out12[5] = (char)('0' + ((hrs/100)%10));
        out12[6] = (char)('0' + ((hrs/10)%10));
        out12[7] = (char)('0' + (hrs%10));
    } else if (hrs >= 10) {
        out12[5] = ' ';
        out12[6] = (char)('0' + ((hrs/10)%10));
        out12[7] = (char)('0' + (hrs%10));
    } else {
        out12[5] = ' ';
        out12[6] = ' ';
        out12[7] = (char)('0' + (hrs%10));          // 예: "  0"
    }

    out12[8]  = blink_colon ? ':' : ' ';            // 콜론 점멸
    out12[9]  = (char)('0' + (mins/10)%10);         // 분 2자리
    out12[10] = (char)('0' + (mins%10));
}


static void TS_Format_mm_ss_cs(uint32_t ms, char* out12 /*>=13*/) {
    uint32_t t = ms;
    uint32_t mm = (t / 60000U) % 100U;
    t %= 60000U;
    uint32_t ss = t / 1000U;
    uint32_t cs = (t % 1000U) / 10U; // 1/100초

    // 12칸(열3~14) 버퍼 채움. [0]은 화면상 2,3 위치.
    // 예: " 12:34.56 " (좌/우 여백 유지)
    snprintf(out12, 13, " %02lu:%02lu.%02lu ", (unsigned long)mm, (unsigned long)ss, (unsigned long)cs);
    size_t len = strlen(out12);
    while (len < 12) out12[len++] = ' ';
    out12[12] = 0;
}

// 2행 3~14에 대해 변경 칸만 덮어쓰기
// 2행(열3~14): 변경된 칸만 쓰기 + 아이콘(2,3) 제어
// 2행(열3~14): 변경된 칸만 쓰기 + 아이콘(2,3) 제어
static void TS_UpdateSlice_ChangedOnly(const char* new12, uint8_t show_clock_icon) {
    // (2,3) 위치 처리
    if (show_clock_icon) {
        if (!s_icon_on_prev || !s_line2_cache_valid || s_line2_cache[0] != new12[0]) {
            LCD16X2_DisplayCustomChar(0, 2, LCD_ROW2_START_COL, CLOCK_ICON_IDX);
            s_icon_on_prev = 1;
        }
    } else {
        // ★아이콘 OFF일 땐 '공백'이 아니라 new12[0]을 그대로 써서 첫 글자(예: 'T')가 나오게 함
        if (s_icon_on_prev || !s_line2_cache_valid || s_line2_cache[0] != new12[0]) {
            LCD16X2_Set_Cursor(MyLCD, 2, LCD_ROW2_START_COL);
            char c0[2] = { new12[0], 0 };
            LCD16X2_Write_String(MyLCD, c0);
            s_icon_on_prev = 0;
        }
    }

    // 나머지 열4~14 (out12[1]~[11])
    for (int i = 1; i < LCD_ROW2_WIDTH; ++i) {
        char prev = s_line2_cache[i];
        char curr = new12[i];
        if (!s_line2_cache_valid || prev != curr) {
            LCD16X2_Set_Cursor(MyLCD, 2, (uint8_t)(LCD_ROW2_START_COL + i));
            char tmp[2] = { curr, 0 };
            LCD16X2_Write_String(MyLCD, tmp);
        }
    }

    // 캐시 갱신
    for (int i=0;i<LCD_ROW2_WIDTH;i++) s_line2_cache[i] = new12[i];
    s_line2_cache_valid = 1;
}

// 메뉴 인덱스에 따른 12칸 버퍼 구성
// 메뉴 인덱스에 따른 12칸 버퍼 구성
static void TS_BuildLine(char* out12 /*>=13*/, uint8_t* out_need_clock_icon) {
    *out_need_clock_icon = 0;
    for (int i=0;i<12;i++) out12[i] = ' ';
    out12[12] = 0;

    switch (g_pract_menu_index) {
        case PRACT_TM_ITEM_DIRECT_OUT:    // 기존 DIRECT OUT → 세션 타이머
        case PRACT_TM_ITEM_24B48K: {      // 기존 24b/48k → 세션 타이머
            *out_need_clock_icon = 1;

            // 원래는 g_sw_session_ms 를 보여줬는데
            // 이제는 RTC로 설정해둔 현재 시각(H:MM)을 보여준다.
            // TS_Format_session_HH_MM_nolead() 가 ms 기준이라서
            // hour/min 을 ms로 뻥튀기해서 넘긴다.
            uint32_t rtc_ms = (uint32_t)g_rtc_hour * 3600000UL
                            + (uint32_t)g_rtc_min  * 60000UL;


            // 세션 타이머: TOTAL과 함께 오르되, 다른 전역(g_sw_session_ms)으로 관리
            // 콜론은 UITotalBlinkStatus에 맞춰 점멸
            TS_Format_session_HH_MM_nolead(rtc_ms, out12, (uint8_t)UITotalBlinkStatus);
            break;
        }

        case PRACT_TM_ITEM_SW_NOW: {
            // 현재(TRIP) 스톱워치: 000'00" (여기서는 기존 구현을 유지)
            *out_need_clock_icon = 1;
            // ... (파일에 이미 들어있는 NOW 포맷/점멸 규칙 그대로 유지)
            // 이 함수의 다른 케이스(타이머, RESET 문구 등)는 기존 로직 유지
            // 필요 시 out12를 구성하는 기존 코드를 그대로 두세요.
            break;
        }

        default:
            // 나머지 메뉴(타이머 세팅/리셋/표시 등) 기존 문자열 유지
            // (이미 main.c 내 다른 분기에서 out12를 구성하고 있으므로 비워둠)
            break;
    }
}


// PracticeHome 루프 안에서 “매 프레임” 호출 (FirstRunFlag와 무관)
// ─────────────────────────────────────────────────────────────────────
// TIMER SETTING 전용 미니 상태기계 (CutOffOnOff==0 & g_timer_set_mode==1 일 때만 호출)
// - Rotary3: 분(1..99) 증감
// - Button[2] SHORT: 값 확정 → 남은시간 갱신, 설정모드 종료
// - UI: "-XX'00\"" 포맷, XX만 UITotalBlinkStatus로 점멸
// - 호출자는 매 프레임 1회 호출(SET 모드일 때만)
// - 이 함수는 자체적으로 이벤트를 '소비'하지 않음(상위에서 clear)
// ─────────────────────────────────────────────────────────────────────
static void TS_TimerSetting_Service(void)
{
    // 메뉴 강제 고정
    g_pract_menu_index = PRACT_TM_ITEM_TIMER_MIN;

    // 1) Rotary3로 1..99 조정
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        rotaryEvent3 = ROTARY_EVENT_NONE;
        if (g_timer_set_min < 99) g_timer_set_min++;
        notch_ui_rotary_click_freq(1200.f); notch_ui_button_beep();
        g_pract_menu_dirty = 1;
    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        rotaryEvent3 = ROTARY_EVENT_NONE;
        if (g_timer_set_min > 1) g_timer_set_min--;  // 최소 1
        notch_ui_rotary_click_freq(800.f); notch_ui_button_beep();
        g_pract_menu_dirty = 1;
    }

    // 2) UI 강제: "-XX'00\"" (XX만 점멸) → 2행 3~14 변경분만 반영
    char buf12[13]; uint8_t need_icon = 1;
    for (int i=0;i<12;i++) buf12[i] = ' ';
    buf12[12] = 0;

    uint8_t mm = (g_timer_set_min == 0) ? 1 : (g_timer_set_min > 99 ? 99 : g_timer_set_min);
    buf12[5] = '-';
    if (UITotalBlinkStatus) {
        buf12[6] = (char)('0' + (mm/10)%10);
        buf12[7] = (char)('0' + (mm%10));
    } else {
        buf12[6] = ' ';
        buf12[7] = ' ';
    }
    buf12[8]  = '\'';  // 고정
    buf12[9]  = '0';
    buf12[10] = '0';
    buf12[11] = '\"';  // 고정

    TS_UpdateSlice_ChangedOnly(buf12, need_icon);
}
// PracticeHome 루프 안에서 “매 프레임” 호출 (FirstRunFlag와 무관)
// ★ 완전 교체본 ★
// 화면 정책(2행 3~14만 부분 갱신):
//   [A] 타이머 "설정 화면" 활성(g_timer_set_mode==1) ▶ (2,3)=시계아이콘, (2,8)부터 "-XX'00\"" (XX만 UITotalBlinkStatus로 점멸)
//   [B] 타이머 비무장 아님(g_timer_remaining_ms>=0) ▶ (2,3)=시계아이콘, (2,8)부터 "-mm'ss" ('/") 점멸, 분 리딩 제로 미표시
//   [C] 그 외 ▶ TS_BuildLine() 결과(원래 메뉴), 필요 시 아이콘 표시
// 누적 로직:
//   - TOTAL: PracticeHome에 있을 때 CutOffOnOff 무관히 증가, 9999:59 초과 시 0 롤오버
//   - TRIP(현재시간): g_sw_trip_run==1일 때만 증가, 999'59" 포화 후 g_sw_now_frozen=1로 정지
//   - TIMER: g_timer_run==1 && remaining>=0 → 카운트다운, 0 도달 시 완료 처리(오디오 정지, 5회 비프+LCD 점멸, 얼림)
// 주의:
//   - g_pract_menu_dirty / 10Hz 타이밍 조건에서만 부분갱신 실행
//   - 어떤 조기 return 경로에서도 g_pract_menu_dirty, g_timer_expired_flag를 정리
// PracticeHome 루프 안에서 “매 프레임” 호출 (FirstRunFlag와 무관)
// PracticeHome 루프 안에서 “매 프레임” 호출 (FirstRunFlag와 무관)
static void Practice_TS_Service_Inline(void)
{

	static uint8_t  s_prev_menu_idx_24 = 0xFF;   // 직전 메뉴 인덱스
	static uint8_t  s_24_text_drawn    = 0;      // 24B48K 모드에서 DIRECT OUT을 1회 그렸는가


    // ───────────────── 시간계 누적 ─────────────────
    uint32_t now = HAL_GetTick();
    static uint32_t s_last_tick_ms = 0;     // 프레임 dt 계산
    uint32_t dt  = (s_last_tick_ms == 0) ? 0 : (now - s_last_tick_ms);
    s_last_tick_ms = now;



    // ── (A) DIRECT 진입/복귀 감지: 캐시 리셋 + 세션 타이머 기본 선택 ──
    static uint8_t s_prev_direct = 0;  // ★ 중복 정의 제거: 이 함수 내 단 한 번만 선언
    uint8_t in_direct = (currentUIState == UI_PRACTICE_HOME) && (CutOffOnOff == 0);
    if (in_direct && !s_prev_direct) {
        // 다음 프레임에 바로 세션 타이머가 뜨도록 준비
        s_line2_cache_valid = 0;
        s_icon_on_prev      = 255;     // 강제 미스매치
        g_pract_menu_index  = PRACT_TM_ITEM_DIRECT_OUT; // ★ 기본: 세션 타이머 슬롯
        g_pract_menu_dirty  = 1;
    }
    s_prev_direct = in_direct;

    // ── (B) 시간 누적(Practice 화면에 있는 동안) ──
    if (currentUIState == UI_PRACTICE_HOME) {
        // TOTAL
        g_sw_total_ms += dt;
        uint32_t hrs  = g_sw_total_ms / 3600000UL;
        uint32_t mins = (g_sw_total_ms % 3600000UL) / 60000UL;
        if (hrs > 9999UL || (hrs == 9999UL && mins > 59UL)) {
            g_sw_total_ms = 0;
        }

        // SESSION (이번 요구 반영: 99:00 초과 시 0:00)
        g_sw_session_ms += dt;
        uint32_t s_hrs  = g_sw_session_ms / 3600000UL;
        uint32_t s_mins = (g_sw_session_ms % 3600000UL) / 60000UL;
        if (s_hrs > 99UL || (s_hrs == 99UL && s_mins > 0UL)) {
            g_sw_session_ms = 0;
        }

        Clock_UpdateCache();

    }

    // (C) TRIP(현재 스톱워치): 달릴 때만 증가. 999'59"에서 freeze (원본 유지)
    const uint32_t NOW_MAX_MS = (999UL*60UL + 59UL) * 1000UL;
    if (g_sw_trip_run && !g_sw_now_frozen) {
        if (g_sw_now_ms < NOW_MAX_MS) {
            uint32_t after = g_sw_now_ms + dt;
            if (after >= NOW_MAX_MS) {
                g_sw_now_ms     = NOW_MAX_MS;
                g_sw_now_frozen = 1; // 리셋 전까지 정지
            } else {
                g_sw_now_ms = after;
            }
        } else {
            g_sw_now_frozen = 1;
        }
    }

    // (D) 타이머 카운트다운: 백그라운드 진행 (원본 유지)
    if (!g_timer_finished && g_timer_run && g_timer_remaining_ms >= 0) {
        if (g_timer_remaining_ms > 0) {
            g_timer_remaining_ms -= (int32_t)dt;
            if (g_timer_remaining_ms <= 0) {
                g_timer_remaining_ms = 0;
                g_timer_run      = 0;
                g_timer_finished = 1;

                // 요구: 오디오 정지 + 5회 비프 + LCD 깜빡임 (STUB, 블로킹)
                AudioProcessingIsReady = 0;
                for (int i = 0; i < 10; ++i) {
                    notch_ui_button_beep();
                    LCDColorSet(1);
                    HAL_Delay(80);
                    LCDColorSet(0);
                    HAL_Delay(80);
                }
                LCDColorSet(2);
                g_timer_expired_flag = 1;
            }
        }
    }

    // ─────────────── DIRECT OUT 상태에서만 2행(3~14) UI 처리 ───────────────
    if (!in_direct) return;
    if (s_modeToastActive) return;

    // 10Hz 주기 갱신
    static uint32_t s_next_ui_ms = 0;
    uint8_t timed_refresh = (now >= s_next_ui_ms);
    if (timed_refresh) s_next_ui_ms = now + 100;

    // RESET OK 토스트 유지 시간(원본 유지)
    static uint32_t s_reset_ok_until_ms = 0;
    uint8_t reset_ok_active = (now < s_reset_ok_until_ms);

    // ─────────────── 2행(3~14) ‘선택된 메뉴’ 부분 갱신 ───────────────

    uint8_t menu_changed = (g_pract_menu_index != s_prev_menu_idx_24);

    if (g_pract_menu_dirty || timed_refresh) {
        char out12[13];
        for (int i=0;i<12;i++) out12[i] = ' ';
        out12[12] = 0;
        uint8_t need_clock_icon = 0;
        uint8_t skip_commit = 0;

        switch (g_pract_menu_index) {

        // ★ 여기: 세션타이머 슬롯을 “RTC 시각”으로 바꾼다
        case PRACT_TM_ITEM_DIRECT_OUT: {
            need_clock_icon = 1;

            if (timed_refresh) {
                Clock_UpdateCache();
            }

            uint8_t h = g_rtc_hour;
            uint8_t m = g_rtc_min;

            // 9,10번째 칸 = out12[6], out12[7]
            if (h >= 10) {
                out12[6] = '0' + (h / 10);
                out12[7] = '0' + (h % 10);
            } else {
                out12[6] = ' ';                 // 리딩제로 대신 공백
                out12[7] = '0' + h;
            }

            // 11번째 칸 = 콜론
            out12[8] = UITotalBlinkStatus ? ':' : ' ';

            // 12,13번째 칸 = 분 (항상 2자리)
            out12[9]  = '0' + ((m / 10) % 10);
            out12[10] = '0' + (m % 10);

            // 14번째 칸은 비워둠
            out12[11] = ' ';
        } break;


        case PRACT_TM_ITEM_24B48K: {
            // 이 케이스에서는 오버레이 커밋을 막고, 진입 시 1회만 직접 그린다.
            need_clock_icon = 0;

            if (menu_changed || !s_24_text_drawn) {
                // 2행 (2,3)부터 정확히 12칸 폭으로 "DIRECT OUT" 1회 출력
                LCD16X2_Set_Cursor(MyLCD, 2, 3);
                LCD16X2_Write_String(MyLCD, "DIRECT OUT   "); // 12칸 패딩
                s_24_text_drawn = 1;

                // 캐시를 ‘지금 화면 상태’로 재동기화하고 싶다면 여기서 캐시를 채워도 됨.
                // 최소 변경 원칙으로는 캐시 무효화만 해두고 커밋을 완전히 스킵한다.
                s_line2_cache_valid = 0;
            }

            // 이 모드에서는 타이머/스톱워치 커밋 로직이 out12를 비워 지워버릴 수 있으므로,
            // 현재 프레임 커밋 자체를 건너뛴다(직접 그린 화면을 유지).
            skip_commit = 1;
        } break;


        case PRACT_TM_ITEM_SW_NOW: {
            need_clock_icon = 1;

            uint32_t ms   = g_sw_now_ms;
            uint32_t mins = ms / 60000UL;          // 0..999
            uint32_t secs = (ms % 60000UL) / 1000; // 0..59

            if (mins >= 100) {
                out12[5] = '0' + (char)((mins / 100UL) % 10UL);
                out12[6] = '0' + (char)((mins /  10UL) % 10UL);
                out12[7] = '0' + (char)( mins            % 10UL);
            } else if (mins >= 10) {
                out12[5] = ' ';
                out12[6] = '0' + (char)((mins / 10UL) % 10UL);
                out12[7] = '0' + (char)( mins         % 10UL);
            } else {
                out12[5] = ' ';
                out12[6] = ' ';
                out12[7] = '0' + (char)(mins % 10UL);
            }

            // (요구 유지) 달릴 때만 깜빡 → 이미 원본 로직대로
            uint8_t run_on = (g_sw_trip_run && !g_sw_now_frozen);
            out12[8]  = run_on ? (UITotalBlinkStatus ? '\'' : ' ') : '\'';
            out12[9]  = '0' + (char)((secs / 10UL) % 10UL);
            out12[10] = '0' + (char)( secs        % 10UL);
            out12[11] = run_on ? (UITotalBlinkStatus ? '\"' : ' ') : '\"';
        } break;

        case PRACT_TM_ITEM_SW_TOTAL: {
            need_clock_icon = 1;
            uint32_t ms   = g_sw_total_ms;
            uint32_t thrs = ms / 3600000UL;                // 0..9999
            uint32_t tmin = (ms % 3600000UL) / 60000UL;    // 0..59

            out12[1] = 'T'; out12[2] = 'O'; out12[3] = 'T'; out12[4] = ' ';
            out12[5] = '0' + (char)((thrs / 1000UL) % 10UL);
            out12[6] = '0' + (char)((thrs /  100UL) % 10UL);
            out12[7] = '0' + (char)((thrs /   10UL) % 10UL);
            out12[8] = '0' + (char)( thrs            % 10UL);
            out12[9]  = UITotalBlinkStatus ? ':' : ' ';
            out12[10] = '0' + (char)((tmin / 10UL) % 10UL);
            out12[11] = '0' + (char)( tmin        % 10UL);
        } break;

        case PRACT_TM_ITEM_TIMER_MIN: {
            need_clock_icon = 1;

            if (g_timer_set_mode == 1) {
                uint8_t mm = (g_timer_set_min == 0) ? 1 : (g_timer_set_min > 99 ? 99 : g_timer_set_min);
                out12[5] = '-';
                if (UITotalBlinkStatus) {
                    out12[6] = '0' + (char)((mm / 10) % 10);
                    out12[7] = '0' + (char)( mm % 10);
                } else {
                    out12[6] = ' ';
                    out12[7] = ' ';
                }
                out12[8]  = '\''; // 고정
                out12[9]  = '0';
                out12[10] = '0';
                out12[11] = '\"'; // 고정
            } else {

                uint8_t tmr_run_on = (g_timer_run && !g_timer_finished && (g_timer_remaining_ms >= 0));

                uint32_t rem_ms = (g_timer_remaining_ms >= 0) ? (uint32_t)g_timer_remaining_ms
                                                             : (uint32_t)g_timer_set_min * 60000UL;
                uint32_t tmins = rem_ms / 60000UL;            // 0..99
                uint32_t tsecs = (rem_ms % 60000UL) / 1000UL; // 0..59

                out12[5] = '-';
                out12[6] = (tmins >= 10) ? ('0' + (char)((tmins / 10UL) % 10UL)) : ' ';
                out12[7] = '0' + (char)(tmins % 10UL);
                out12[8]  = tmr_run_on ? (UITotalBlinkStatus ? '\'' : ' ') : '\'';
                out12[9]  = '0' + (char)((tsecs / 10UL) % 10UL);
                out12[10] = '0' + (char)( tsecs        % 10UL);
                out12[11] = tmr_run_on ? (UITotalBlinkStatus ? '\"' : ' ') : '\"';
            }
        } break;

        case PRACT_TM_ITEM_TIMER_RESET: {
            const char *s = reset_ok_active ? "  RESET OK " : "TIMER RESET?";
            for (int i=0; i<12 && s[i]; ++i) out12[i] = s[i];
        } break;

        case PRACT_TM_ITEM_TIMER_SET: {
            const char *s = "TIMER SET? ";
            for (int i=0; i<12 && s[i]; ++i) out12[i] = s[i];
        } break;

        case PRACT_TM_ITEM_SW_RESET: {
            const char *s = reset_ok_active ? "  RESET OK " : "SW  RESET?";
            for (int i=0; i<12 && s[i]; ++i) out12[i] = s[i];
        } break;
        }

        if (skip_commit) {
            s_prev_menu_idx_24 = g_pract_menu_index; // 이전 인덱스 갱신
            g_pract_menu_dirty = 0;
            return; // 이번 프레임 커밋 생략(직접 그린 DIRECT OUT을 유지)
        }

        TS_UpdateSlice_ChangedOnly(out12, need_clock_icon);

        TS_UpdateSlice_ChangedOnly(out12, need_clock_icon);
        s_prev_menu_idx_24 = g_pract_menu_index;  // 마지막에 항상 갱신
        g_pract_menu_dirty = 0;
    }
}






void PracticeHome(void) {
    // 부분 갱신용 캐시(깜빡임 방지)
    static int8_t  prevCutOffOnOff = -1;
    static int8_t  prevPitchSemi   = -127;
    static int16_t prevInst        = -1;
    static uint32_t prevStart = 0xFFFFFFFF, prevEnd = 0xFFFFFFFF;

    // 한 줄(2행)만 갱신하는 헬퍼
    auto void UpdateSecondLine(void) {

    	//Practice_RegisterChars_ForMode(CutOffOnOff);


    	if (s_modeToastActive) return; // 토스트 중이면 2행은 토스트가 다 씀 → 아이콘 그리지 않음

        // 공통: 2행, 1열 아이콘/색
        if (CutOffOnOff == 0) {


            LCDColorSet(2);
           LCD16X2_DisplayCustomChar(0, 2, 1, 3); // Loopback


        	LCD16X2_Set_Cursor(MyLCD, 2, 3);
        	LCD16X2_Write_String(MyLCD, "             ");


        }
        else if (CutOffOnOff == 1) {
            LCDColorSet(3);
            // (2,1) 멜로디 아이콘
            LCD16X2_DisplayCustomChar(0, 2, 1, 4);

            // (2,2) 잔상 한 칸 비우기
            LCD16X2_Set_Cursor(MyLCD, 2, 2);
            LCD16X2_Write_String(MyLCD, " ");

            // 오직 2,3~2,15 구간만 덮어쓰기 (2,16 아이콘은 건드리지 않음)
            LCD16X2_Set_Cursor(MyLCD, 2, 3);
            LCD16X2_Write_String(MyLCD, "             "); // 정확히 13칸 (3..15)

            LCD16X2_Set_Cursor(MyLCD, 2, 3);
            if (CurrentInstrumentType == Instrument_USER_PRESET_1 ||
                CurrentInstrumentType == Instrument_USER_PRESET_2 ||
                CurrentInstrumentType == Instrument_USER_PRESET_3) {
                // User Preset이면 범위를 그대로 표시 (Hz 포함)
                char buf[16];
                snprintf(buf, sizeof(buf), "%4d-%4dHz", CutOffFreqStart, CutOffFreqEnd);
                LCD16X2_Write_String(MyLCD, buf);
            } else {
                // 악기명 테이블 (최대 12자 가정)
                LCD16X2_Write_String(MyLCD, InstrumentNameTable[CurrentInstrumentType]);
            }

            // (2,16) 재생/정지 아이콘은 이 블록에서 절대 손대지 않음!
        }

        else if (CutOffOnOff == 2) { // Pitch Shift UI (원래 의도대로 복원)
            LCDColorSet(1);
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_DisplayCustomChar(0, 2, 1, 5); // TuningFork
            // 좌우 화살표
            LCD16X2_Set_Cursor(MyLCD, 2, 6);  LCD16X2_Write_String(MyLCD, "<");
            LCD16X2_Set_Cursor(MyLCD, 2, 14); LCD16X2_Write_String(MyLCD, ">");

            // 좌측 중앙 표시 영역(열 3~5) 클리어 후 각 케이스 출력
            LCD16X2_Set_Cursor(MyLCD, 2, 3);
            LCD16X2_Write_String(MyLCD, "   "); // 잔상 제거

            switch (PitchSemitone) {
                case 0: // -3  → "# 3" (원래 코드: 3을 별도 칸에)
                    LCD16X2_DisplayCustomChar(0, 2, 3, 2); // 원래 쓰던 커스텀(샾 비슷한 기호 역할)
                    LCD16X2_Set_Cursor(MyLCD, 2, 4);
                    LCD16X2_Write_String(MyLCD, "3");
                    break;
                case 1: // -2  → "# 2"
                    LCD16X2_DisplayCustomChar(0, 2, 3, 2);
                    LCD16X2_Set_Cursor(MyLCD, 2, 4);
                    LCD16X2_Write_String(MyLCD, "2");
                    break;
                case 2: // -1  → "# 1"
                    LCD16X2_DisplayCustomChar(0, 2, 3, 2);
                    LCD16X2_Set_Cursor(MyLCD, 2, 4);
                    LCD16X2_Write_String(MyLCD, "1");
                    break;
                case 3: //  0  → " 0" (왼쪽 칸 비우고 0만)
                    LCD16X2_Set_Cursor(MyLCD, 2, 4);
                    LCD16X2_Write_String(MyLCD, "0");
                    break;
                case 4: // +1  → "#1"
                    LCD16X2_Set_Cursor(MyLCD, 2, 3);
                    LCD16X2_Write_String(MyLCD, "#1");
                    break;
                case 5: // +2  → "#2"
                    LCD16X2_Set_Cursor(MyLCD, 2, 3);
                    LCD16X2_Write_String(MyLCD, "#2");
                    break;
                case 6: // +3  → "#3"
                    LCD16X2_Set_Cursor(MyLCD, 2, 3);
                    LCD16X2_Write_String(MyLCD, "#3");
                    break;
                default:
                    // 범위 밖 값 보호(이상 상황)
                    LCD16X2_Set_Cursor(MyLCD, 2, 3);
                    LCD16X2_Write_String(MyLCD, " 0");
                    break;
            }

            // 게이지(열 7~13): 해당 위치만 채우고 나머진 비움 (원래 매핑: 7 + PitchSemitone)
            for (int col = 7; col <= 13; ++col) {
                uint8_t glyph = (col == (7 + PitchSemitone)) ? PITCH_MARK_IDX : PITCH_BAR_IDX; // 4=체크, 3=바
                LCD16X2_DisplayCustomChar(0, 2, col, glyph);
            }

        }

    }

    // === [PATCH] 토스트 유지 중에도 '가운데 버튼 짧게(Play/Pause)'는 즉시 통과 ===
    if (s_modeToastActive) {
        if ((int32_t)(HAL_GetTick() - s_modeToastDeadline) >= 0) {
            // 토스트 종료: 원래 2행 복귀 + 아이콘 복구 + 캐시 동기
            s_modeToastActive = 0;

            Practice_RegisterChars_ForMode(CutOffOnOff);


            UpdateSecondLine();
            Practice_DrawPlayPauseIcon_IfAllowed(); // 토스트 끝났으니 이제 그려짐

            prevCutOffOnOff = CutOffOnOff;
            prevPitchSemi   = PitchSemitone;
            prevInst        = (int16_t)CurrentInstrumentType;
            prevStart       = CutOffFreqStart;
            prevEnd         = CutOffFreqEnd;
        } else {
            // ★ 여기서부터가 핵심: 토스트 유지 중이라도 Play/Pause는 즉시 반영
            if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
                buttonEvents[0] = BUTTON_EVENT_NONE;
                AudioProcessingIsReady ^= 1;

                // 토스트가 2행을 차지 중이라도 '2,16' 아이콘 칸만 즉시 갱신
                // (토스트 문구 손상 없이 우측 끝 칸만 바꿔 시각 피드백을 줌)
                LCD16X2_DisplayCustomChar(0, 2, 16, (AudioProcessingIsReady ? 0 : 1));
            }

            // 나머지 입력은 그대로 막아서 토스트 중 UI 오염 방지
            rotaryEvent3 = ROTARY_EVENT_NONE;
            buttonEvents[1] = BUTTON_EVENT_NONE;
            buttonEvents[2] = BUTTON_EVENT_NONE;
        }
        return;
    }

    // --- Mini VU(특히 I2S2) 갱신을 위해 Practice 화면에선 상시 계산 ON
    notch_set_vu_enabled(1);

    // --- [AUTO VU] VU 상태에서 "모드/선택/메뉴 버튼" 또는 "UI 값 로터리"가 움직이면 즉시 탈출
    // 요구사항: 버튼(모드/선택/메뉴 역할) + UI 로터리(rotaryEvent3) 감지 시 곧바로 VU 종료
    if (g_vu_active && AutoVU_After10s)
    {



        uint8_t exit_by_button = (buttonEvents[2] != BUTTON_EVENT_NONE); // 보통 '모드/선택/메뉴' 역할
        uint8_t exit_by_rotary = (rotaryEvent3    != ROTARY_EVENT_NONE); // UI 값을 바꾸는 로터리

        if (exit_by_button || exit_by_rotary) {
            // 이벤트 소비
            if (exit_by_button) buttonEvents[2] = BUTTON_EVENT_NONE;
            if (exit_by_rotary) rotaryEvent3    = ROTARY_EVENT_NONE;

            // VU OFF (기존 종료 시퀀스와 동일하게 처리)
            g_vu_active = 0;
            notch_set_vu_enabled(0);

            // 1) VU 전용 커스텀 문자 제거
            LCD16X2_ClearCustomChars(0);
            // 2) Practice 홈을 '처음 실행' 상태로 만들어, 그쪽에서 쓰는 커스텀/아이콘 재등록
            PracticeHomeFirstRunFlag = 0;
            // 3) 백라이트 복구
            LCDColorSet(g_vu_prev_color);

            // 다음 루프에서 PracticeHome()이 처음부터 다시 그림
            return;
        }
    }


    // === [AUTO VU] 입력 스니핑: 버튼/UI 로터리에 변화가 있으면 "최근 상호작용 시각" 갱신
    if (buttonEvents[0] != BUTTON_EVENT_NONE ||
        buttonEvents[1] != BUTTON_EVENT_NONE ||
        buttonEvents[2] != BUTTON_EVENT_NONE ||
        rotaryEvent3    != ROTARY_EVENT_NONE) {
        g_practice_last_ui_ms = HAL_GetTick();
    }

    // 최초 진입 1회에 기준 시각 잡기 (첫 렌더 직전에)
    if (!PracticeHomeFirstRunFlag) {
        g_practice_last_ui_ms = HAL_GetTick();
    }

    // === [AUTO VU] '20000' 초 무입력 시 자동 진입 (설정이 켜져 있고, 아직 VU 비활성일 때)
    //
    if (AutoVU_After10s && !g_vu_active) {
        uint32_t now_ms = HAL_GetTick();
        if ((int32_t)(now_ms - g_practice_last_ui_ms) >= 20000) { // 이 값을 바꿔서 초를 설정하십시오.
            // 자동으로 VU ON
            g_vu_active = 1;
            g_vu_inited = 0;
            notch_set_vu_enabled(1);
            // 바로 1프레임 그리기 (기존 스타일 그대로)
            Practice_VUMeter_App();
            // 여기서 반환하지 않고 아래 기존 루틴을 그대로 흐르게 해도 OK.
        }
    }



    Practice_TS_Service_Inline();

    // ===== 최초 1회 렌더(정적 요소) =====
    if (!PracticeHomeFirstRunFlag) {
        LCD16X2_Clear(MyLCD);
        LCD16X2_ClearCustomChars(0);

        Practice_RegisterChars_ForMode(CutOffOnOff);

        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "PRACTICE MODE      ");

        //Practice_DrawMiniVU_1stRow_13_16(); // 이것이 호출되서 VU가 가리기 때문에 삭제함.

        // 2행 초기 내용
        UpdateSecondLine();

        if (CutOffOnOff == 0) {
            s_line2_cache_valid = 0;
            s_icon_on_prev      = 255; // 강제 미스매치
            g_pract_menu_index  = PRACT_TM_ITEM_DIRECT_OUT; // 세션 타이머 슬롯
            g_pract_menu_dirty  = 1;
            char buf12[13]; uint8_t need_icon = 0;
            TS_BuildLine(buf12, &need_icon);
            TS_UpdateSlice_ChangedOnly(buf12, need_icon);
        }

        // 캐시 초기화
        prevCutOffOnOff = CutOffOnOff;
        prevPitchSemi   = PitchSemitone;
        prevInst        = (int16_t)CurrentInstrumentType;
        prevStart       = CutOffFreqStart;
        prevEnd         = CutOffFreqEnd;


        s_practiceCharsReady = 1;               // ★ 이제 토스트 안전


        /*
        // ★ 최초 진입 직후에 ‘보류된’ 토스트가 있으면 여기서 1회 실행
        // ★ 첫 진입 시 현재 모드 토스트를 '정확히 한 번만' 띄운다.
        Practice_ShowModeToast(CutOffOnOff);

        // 기존 pending 토스트가 있으면 처리(있어도 중복 없이 1회만)
        if (s_toastPending) {
            s_toastPending = 0;
            Practice_ShowModeToast(s_toastPendingMode);
        }
        */


        PracticeHomeFirstRunFlag = 1;
    }


    // ===== 우측 재생/일시정지 아이콘(항상 유지, 토스트 중 자동 억제) =====
    Practice_DrawPlayPauseIcon_IfAllowed();

    // 토스트가 없을 때만 1행 미니 VU 두 개 동작 (전용 VU 앱이 켜져 있을 땐 숨김)
    if (!g_vu_active && !s_modeToastActive) {
        Practice_DrawMiniVU_I2S2_1stRow_1_7();   // '1' + [2~7] I2S2 VU
        Practice_DrawMiniVU_1stRow_13_16();      // '2' + [11~16] I2S3 VU (MUTE 포함)
    }




    if (TS_InPracticeDirect()) {

        // ─────────────────────────────────────────────────────────────
        // [1] 버튼2(Short) 처리: SET 화면 진입/확정, 타이머 토글, TRIP 토글
        // ─────────────────────────────────────────────────────────────
    	if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
    	    // ★ DIRECT(시계/타이머) 전용 처리
    	    if (TS_InPracticeDirect()) {
    	        // (A) 현재 '설정 모드'인 경우: 버튼[2] = 확정 & 종료
    	        if (g_timer_set_mode == 1) {
    	            buttonEvents[2] = BUTTON_EVENT_NONE;

    	            if (g_timer_set_min == 0) g_timer_set_min = 1;
    	            g_timer_remaining_ms = (int32_t)(g_timer_set_min * 60000UL);
    	            g_timer_finished     = 0;
    	            g_timer_run          = 0;                 // 설정 직후엔 정지 상태
    	            g_timer_set_mode     = 0;                 // 설정 모드 종료
    	            g_pract_menu_index   = PRACT_TM_ITEM_TIMER_MIN; // 타이머 화면으로 유지
    	            g_pract_menu_dirty   = 1;
    	            notch_ui_button_beep();
    	            // 설정모드에서 벗어났으니, 이후 분기는 타지 않게 즉시 리턴
    	            return;
    	        }

    	        // (B) 아직 '설정 모드'가 아니고, 메뉴가 "TIMER SET?" 이면: 버튼[2] = 설정 모드로 진입
    	        if (g_pract_menu_index == PRACT_TM_ITEM_TIMER_SET) {
    	            buttonEvents[2] = BUTTON_EVENT_NONE;
    	            g_timer_set_mode  = 1;
    	            if (g_timer_set_min == 0) g_timer_set_min = 1;
    	            g_pract_menu_index = PRACT_TM_ITEM_TIMER_MIN; // 설정화면 슬롯
    	            g_pract_menu_dirty = 1;
    	            notch_ui_button_beep();
    	            return;
    	        }

    	        // (C) 업카운트(TRIP) 슬롯에서 버튼[2]: 실행/일시정지 토글
    	        if (g_pract_menu_index == PRACT_TM_ITEM_SW_NOW) {
    	            buttonEvents[2] = BUTTON_EVENT_NONE;
    	            if (!g_sw_now_frozen) {
    	                g_sw_trip_run = !g_sw_trip_run;
    	                g_pract_menu_dirty = 1;
    	                notch_ui_button_beep();
    	            }
    	            return;
    	        }

    	        // (D) 다운카운트 타이머 슬롯에서 버튼[2]: 시작/일시정지 토글
    	        if (g_pract_menu_index == PRACT_TM_ITEM_TIMER_MIN) {
    	            buttonEvents[2] = BUTTON_EVENT_NONE;
    	            if (g_timer_remaining_ms < 0) {
    	                // 비무장 상태면 값에서 무장
    	                g_timer_remaining_ms = (int32_t)(g_timer_set_min * 60000UL);
    	                g_timer_finished     = 0;
    	            }
    	            if (!g_timer_finished) {
    	                g_timer_run = !g_timer_run;           // ← ★ 여기서 반드시 토글
    	                g_pract_menu_dirty = 1;
    	                notch_ui_button_beep();
    	            }
    	            return;
    	        }
    	    }
    	}

        // ─────────────────────────────────────────────────────────────
        // [2] 버튼0(ENTER, Short) 처리: 기존 호환 동작만 유지(필요 최소)
        //     - 요구 변경사항 기준으론 버튼2가 메인이지만,
        //       기존 패턴과 충돌 없도록 최소 동작만 남겨둔다.
        // ─────────────────────────────────────────────────────────────
        if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
            // TIMER RESET? → 완전 리셋
            if (g_pract_menu_index == PRACT_TM_ITEM_TIMER_RESET) {
                buttonEvents[2]      = BUTTON_EVENT_NONE;
                g_timer_run          = 0;
                g_timer_finished     = 0;
                g_timer_remaining_ms = -1;  // 비무장
                g_pract_menu_dirty   = 1;
                notch_ui_button_beep();
            }
            // STOPWATCH RESET? → 현재(TRIP)만 리셋
            else if (g_pract_menu_index == PRACT_TM_ITEM_SW_RESET) {
                buttonEvents[2]    = BUTTON_EVENT_NONE;
                g_sw_now_ms        = 0;
                g_sw_now_frozen    = 0;     // 언프리즈
                g_pract_menu_dirty = 1;
                notch_ui_button_beep();
            }
            // TIMER SET?은 버튼2로 처리하므로 여기선 소비만
            else if (g_pract_menu_index == PRACT_TM_ITEM_TIMER_SET) {
                buttonEvents[2] = BUTTON_EVENT_NONE; // 무시
            }
            else {
                // 다른 컨텍스트면 소비만
                buttonEvents[2] = BUTTON_EVENT_NONE;
            }
        }

        // ─────────────────────────────────────────────────────────────
        // [3] 로터리3(CW/CCW) 처리
        //     - 설정 화면(g_timer_set_mode==1): 메뉴 이동 금지, 1..99에서 분값만 증감
        //     - 그 외: 메뉴 인덱스 ±1 랩어라운드
        //     - 메뉴4(분설정)는 설정화면이 아닐 때 0..99 증감(기존 패턴 유지)
        // ─────────────────────────────────────────────────────────────
        {
            RotaryEvent ev3 = rotaryEvent3; // 스냅샷 (한 번만 읽고 마지막에 클리어)
            if (ev3 == ROTARY_EVENT_CW || ev3 == ROTARY_EVENT_CCW) {

                const float click_freq = (ev3 == ROTARY_EVENT_CW) ? 1200.f : 800.f;

                if (g_timer_set_mode == 1) {
                    // 설정 화면: 1..99 범위에서만 증감
                    if (ev3 == ROTARY_EVENT_CW  && g_timer_set_min < 99) g_timer_set_min++;
                    if (ev3 == ROTARY_EVENT_CCW && g_timer_set_min >  1) g_timer_set_min--;
                    g_pract_menu_dirty = 1;

                } else {
                    // 일반 상태: 메뉴 인덱스 랩 이동
                    if (ev3 == ROTARY_EVENT_CW) {
                        g_pract_menu_index = (uint8_t)((g_pract_menu_index + 1) % 8);
                    } else {
                        g_pract_menu_index = (uint8_t)((g_pract_menu_index + 7) % 8);
                    }
                    g_pract_menu_dirty = 1;
                }

                // 이벤트 소비
                rotaryEvent3 = ROTARY_EVENT_NONE;
            }
        }


        if (TS_InPracticeDirect() && g_timer_set_mode == 1) {
            TS_TimerSetting_Service();
            // 설정 모드에서는 여기서 2행을 이미 그리고 끝냈으므로 추가 처리 없이 빠져나감
            // (다른 2행 갱신 루틴에 의해 덮이지 않도록 상위 루프에서 return 하거나,
            //  아래쪽 2행 일괄 갱신 전에 g_pract_menu_dirty를 0으로 둬도 무방)
        }

    }
    // ==================================================================





    // ===== 버튼 동작 =====
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {

        if (CutOffOnOff == 1 && IS_USER_PRESET(CurrentInstrumentType)) {
            CurrentDisplayStatus = 0;
            g_PracticeCutoff_PreserveLine2_OnNextEntry = 1;
            currentUIState = UI_PRACTICE_FREQ_SETTING_CUTOFF_START;
            PracticeHomeFirstRunFlag = 0;
            notch_ui_button_beep();   // (원래 위치상 도달 불가였던 삑 보정)
            buttonEvents[2] = BUTTON_EVENT_NONE;
            return; // ★ 상태 전환 후 즉시 탈출
        }

        buttonEvents[2] = BUTTON_EVENT_NONE;

        // 조건 불만족이면 아무것도 안 함(조용히 소비)
        // 필요하면 여기서 삑을 넣어도 됨.
    }



    // 가운데 버튼 길게: 모드 순환(0→1→2→0…)
    if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[0] = BUTTON_EVENT_NONE;

        // 1) 모드 갱신
        CutOffOnOff = (CutOffOnOff + 1) % 3;

        // ★ [추가] VU 미터가 켜져 있으면 즉시 탈출 (토스트가 보이도록)
        if (g_vu_active) {
            g_vu_active = 0;
            notch_set_vu_enabled(0);       // DSP 쪽 VU 비활성
            //LCD16X2_ClearCustomChars(0);   // VU 커스텀 문자 제거
            Practice_RegisterChars_ForMode(CutOffOnOff);
            PracticeHomeFirstRunFlag = 0;  // Practice 화면을 처음부터 다시 그리게
            LCDColorSet(g_vu_prev_color);  // 백라이트 색 복원
            // (g_vu_inited는 VU 재진입 시 다시 세팅됨)
        }

        // 2) 입력 큐 정리(토스트 동안 추가 입력 무시)
        rotaryEvent3   = ROTARY_EVENT_NONE;
        buttonEvents[1]= BUTTON_EVENT_NONE;
        buttonEvents[2]= BUTTON_EVENT_NONE;

        // 3) 토스트 띄우기(비블로킹 1.5s 유지)
        Practice_ShowModeToast(CutOffOnOff);

        // 4) 캐시만 갱신(토스트 끝나면 정상 라인 재그림)
        prevCutOffOnOff = CutOffOnOff;

        notch_ui_button_beep();

        return; // <- 중요! 토스트 루틴이 최상단에서 처리
    }







    // 가운데 버튼 짧게: 오디오 On/Off 토글(동작 유지)
    if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[0] = BUTTON_EVENT_NONE;
        AudioProcessingIsReady ^= 1;

        notch_ui_button_beep();

        return;
    }

    // 길게(우측 버튼) → 모드 선택으로 복귀(기존)
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2]           = BUTTON_EVENT_NONE;
        ModeSelectionFirstRunFlag = 0;
        PracticeHomeFirstRunFlag  = 0;
        g_vu_active = 0;
        currentUIState            = UI_MODE_SELECTION;

        notch_ui_mode_return_triple_beep();

        return;
    }








    // ===== 로터리: CutOffOnOff 상태별 실시간 분기 =====
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        if (CutOffOnOff == 2) {
            if (PitchSemitone < 6) PitchSemitone++;
            UpdateSecondLine_Mode2_PitchOnly();      // ← 부분 갱신
            prevPitchSemi = PitchSemitone;
        } else if (CutOffOnOff == 1) {
            CurrentInstrumentType = (CurrentInstrumentType + 1) % Instrument_MAX;



            // ▼▼▼ after CurrentInstrumentType moved (wrap handled elsewhere) ▼▼▼
            // 1) map instrument -> (CutOffFreqStart, CutOffFreqEnd)
            switch (CurrentInstrumentType) {
            case Instrument_WIDE_HIGH:     CutOffFreqStart = 1000; CutOffFreqEnd = 8000;  break;
            case Instrument_WIDE_MID:      CutOffFreqStart =  200; CutOffFreqEnd = 2000;  break;
            case Instrument_WIDE_LOW:      CutOffFreqStart =   50; CutOffFreqEnd =  500;  break;

            case Instrument_NARROW_HIGH:   CutOffFreqStart = 2500; CutOffFreqEnd = 4000;  break;
            case Instrument_NARROW_MID:    CutOffFreqStart =  400; CutOffFreqEnd =  800;  break;
            case Instrument_NARROW_LOW:    CutOffFreqStart =   80; CutOffFreqEnd =  200;  break;

            case Instrument_DRUMSET:       CutOffFreqStart =   40; CutOffFreqEnd = 10000; break;
            case Instrument_XYLOPHONE:     CutOffFreqStart =  500; CutOffFreqEnd = 4000;  break;

            case Instrument_USER_PRESET_1: CutOffFreqStart = CutOffFreqStartUser1; CutOffFreqEnd = CutOffFreqEndUser1; break;
            case Instrument_USER_PRESET_2: CutOffFreqStart = CutOffFreqStartUser2; CutOffFreqEnd = CutOffFreqEndUser2; break;
            case Instrument_USER_PRESET_3: CutOffFreqStart = CutOffFreqStartUser3; CutOffFreqEnd = CutOffFreqEndUser3; break;
            default:                       CutOffFreqStart =  200; CutOffFreqEnd = 2000;  break;
            }

            // 2) “2행의 악기/범위만” 갱신 — 아이콘(재생/일시정지)은 건드리지 않음
            UpdateSecondLine_Mode1_InstrumentOnly();  // ← 이 함수가 2행 특정 구간만 업데이트(아이콘 유지). :contentReference[oaicite:5]{index=5}

            prevInst = (int16_t)CurrentInstrumentType;
        }

        notch_ui_rotary_click_freq( 1200.f );

        rotaryEvent3 = ROTARY_EVENT_NONE;


    }


    else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        if (CutOffOnOff == 2) {
            if (PitchSemitone > 0) PitchSemitone--;
            UpdateSecondLine_Mode2_PitchOnly();      // ← 부분 갱신
            prevPitchSemi = PitchSemitone;
        } else if (CutOffOnOff == 1) {
            CurrentInstrumentType = (CurrentInstrumentType == 0) ? (Instrument_MAX - 1) : (CurrentInstrumentType - 1);


            // ▼▼▼ after CurrentInstrumentType moved (wrap handled elsewhere) ▼▼▼
            // 1) map instrument -> (CutOffFreqStart, CutOffFreqEnd)
            switch (CurrentInstrumentType) {
            case Instrument_WIDE_HIGH:     CutOffFreqStart = 1000; CutOffFreqEnd = 8000;  break;
            case Instrument_WIDE_MID:      CutOffFreqStart =  200; CutOffFreqEnd = 2000;  break;
            case Instrument_WIDE_LOW:      CutOffFreqStart =   50; CutOffFreqEnd =  500;  break;

            case Instrument_NARROW_HIGH:   CutOffFreqStart = 2500; CutOffFreqEnd = 4000;  break;
            case Instrument_NARROW_MID:    CutOffFreqStart =  400; CutOffFreqEnd =  800;  break;
            case Instrument_NARROW_LOW:    CutOffFreqStart =   80; CutOffFreqEnd =  200;  break;

            case Instrument_DRUMSET:       CutOffFreqStart =   40; CutOffFreqEnd = 10000; break;
            case Instrument_XYLOPHONE:     CutOffFreqStart =  500; CutOffFreqEnd = 4000;  break;

            case Instrument_USER_PRESET_1: CutOffFreqStart = CutOffFreqStartUser1; CutOffFreqEnd = CutOffFreqEndUser1; break;
            case Instrument_USER_PRESET_2: CutOffFreqStart = CutOffFreqStartUser2; CutOffFreqEnd = CutOffFreqEndUser2; break;
            case Instrument_USER_PRESET_3: CutOffFreqStart = CutOffFreqStartUser3; CutOffFreqEnd = CutOffFreqEndUser3; break;
            default:                       CutOffFreqStart =  200; CutOffFreqEnd = 2000;  break;
            }

            // 2) “2행의 악기/범위만” 갱신 — 아이콘(재생/일시정지)은 건드리지 않음
            UpdateSecondLine_Mode1_InstrumentOnly();  // ← 이 함수가 2행 특정 구간만 업데이트(아이콘 유지). :contentReference[oaicite:5]{index=5}

            prevInst = (int16_t)CurrentInstrumentType;
        }

        notch_ui_rotary_click_freq( 800.f );

        rotaryEvent3 = ROTARY_EVENT_NONE;
    }


    /// 버튼 1(볼륨) : VU 온오프
    // === Practice 모드 안에서만 VU 토글 ===
    if (currentUIState == UI_PRACTICE_HOME)
    {
        // 1) 길게 눌러 활성화
        if (!g_vu_active && buttonEvents[VU_TOGGLE_BTN_IDX] == BUTTON_EVENT_LONG_PRESS) {
            g_vu_active = 1;
            g_vu_inited = 0;
            buttonEvents[VU_TOGGLE_BTN_IDX] = BUTTON_EVENT_NONE;


            notch_ui_button_beep();

            // 슬레이브에 계산 ON
            notch_set_vu_enabled(1);

            // 즉시 그리기 1프레임
            Practice_VUMeter_App();
        }

        // 2) 짧게 눌러 비활성화
        if (g_vu_active && buttonEvents[VU_TOGGLE_BTN_IDX] == BUTTON_EVENT_SHORT_PRESS) {
            g_vu_active = 0;
            buttonEvents[VU_TOGGLE_BTN_IDX] = BUTTON_EVENT_NONE;

            notch_set_vu_enabled(0);

            // 1) VU 전용 커스텀 문자 제거
            LCD16X2_ClearCustomChars(0);

            // 2) Practice 홈을 '처음 실행' 상태로 만들어, 그쪽에서 쓰는 커스텀 문자/아이콘을 재등록하게 함
            PracticeHomeFirstRunFlag = 0;   // ★ 중요: 재등록/전체 리드로우 트리거

            // 3) 백라이트 원상 복구
            LCDColorSet(g_vu_prev_color);

            notch_ui_button_beep();

            // 4) 화면은 다음 루프에서 PracticeHome()이 처음부터 다시 그림
            return;
        }


        // 3) 활성화 상태면 주기 갱신
        if (g_vu_active) {
            Practice_VUMeter_App();
            // 필요 시, 다른 Practice용 HUD와 겹치지 않게 이 구간에서만 그리도록 유지
            // (Play/Pause 아이콘이 우측(2,16)에 갱신될 수 있는데, VU가 2..16을 쓰므로
            //  이 프레임에선 VU가 우선 그려질 수 있음. 충돌되면 VU를 2..15로 바꾸고 16을 아이콘에 양보해도 됨)
        }

        if (!g_vu_active) {
            Practice_DrawMiniVU_1stRow_13_16();
        }

    }


    // VOL 버튼 짧게: 버튼 무음 토글 (미완성)
    // VOL 버튼 짧게: 버튼 무음 토글 (마이크만)
    if (buttonEvents[1] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[1] = BUTTON_EVENT_NONE;

        if (!g_micMuted) {
            g_prevSoundBalance = SoundBalance; // 현재 마이크 볼륨 저장
            SoundBalance = 0;                  // 마이크만 0
            g_micMuted = 1;
        } else {
            SoundBalance = g_prevSoundBalance; // 복구
            g_micMuted = 0;
        }


        notch_ui_button_beep();
        return; // UI는 건드리지 않음 (오디오만 즉시 반영)
    }



    // ✨ 필요 시(값이 외부에서 바뀐 경우) 캐시와 비교해 한 줄만 재그림
    if (prevCutOffOnOff != CutOffOnOff ||
        prevPitchSemi   != PitchSemitone ||
        prevInst        != (int16_t)CurrentInstrumentType ||
        prevStart       != CutOffFreqStart ||
        prevEnd         != CutOffFreqEnd) {
        UpdateSecondLine();
        prevCutOffOnOff = CutOffOnOff;
        prevPitchSemi   = PitchSemitone;
        prevInst        = (int16_t)CurrentInstrumentType;
        prevStart       = CutOffFreqStart;
        prevEnd         = CutOffFreqEnd;
    }
}


// ─────────────────────────────────────────────────────────────
// [PATCH] Cutoff 설정 화면 공용 프레임(1행 제목 + 2행 아이콘/고정부)
//  - (2,16) 재생/일시정지 아이콘은 "상태 기반"으로 표시
//  - (2,2) 잔상 숫자 클리어
//  - User Preset이면 1행 제목에 Preset 번호 붙이기 (START/END FREQ N)
// ─────────────────────────────────────────────────────────────
static void PracticeCutoff_DrawCommonFrame_NoRefresh(const char* baseTitle, uint8_t preserve_line2)
{
    // 색상: 기존 규칙 유지
    if (CutOffOnOff == 0)      LCDColorSet(1);
    else if (CutOffOnOff == 1) LCDColorSet(2);
    else                       LCDColorSet(3);

    // 1행 제목: User Preset인 경우 번호 표기
    char title[17];
    if (CurrentInstrumentType == Instrument_USER_PRESET_1) {
        snprintf(title, sizeof(title), "%s 1", baseTitle);
    } else if (CurrentInstrumentType == Instrument_USER_PRESET_2) {
        snprintf(title, sizeof(title), "%s 2", baseTitle);
    } else if (CurrentInstrumentType == Instrument_USER_PRESET_3) {
        snprintf(title, sizeof(title), "%s 3", baseTitle);
    } else {
        snprintf(title, sizeof(title), "%s", baseTitle);
    }

    // 1행만 제목 갱신 (풀 클리어 금지)
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "                ");
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, title);

    // 좌측 아이콘은 PracticeHome과 동일 위치 유지
    if (CutOffOnOff == 0)      LCD16X2_DisplayCustomChar(0, 2, 1, 3);
    else if (CutOffOnOff == 1) LCD16X2_DisplayCustomChar(0, 2, 1, 4);
    else                       LCD16X2_DisplayCustomChar(0, 2, 1, 5);

    // 볼륨/다른 화면 잔상 제거: (2,2) 한 칸을 공백으로 비워줌
    LCD16X2_Set_Cursor(MyLCD, 2, 2);
    LCD16X2_Write_String(MyLCD, " ");

    // (2,16) 재생/일시정지 아이콘은 상태에 맞게!
    // (이 줄이 문제였음: 이전에는 무조건 Paused(1)로 박혀있었음)
    Practice_DrawPlayPauseIcon_IfAllowed();  // 내부에서 2,16에 적절한 커스텀 캐릭을 그려줌

    // "Hz" 고정
    LCD16X2_Set_Cursor(MyLCD, 2, 12);
    LCD16X2_Write_String(MyLCD, "Hz");

    // 값 라인(2,3~)은 필요 시에만 초기 표시 (잔상 최소화)
    if (!preserve_line2) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%4d-%4d", (int)CutOffFreqStart, (int)CutOffFreqEnd);
        LCD16X2_Set_Cursor(MyLCD, 2, 3);
        LCD16X2_Write_String(MyLCD, buf);
    }
}


void PracticeFreqSettingCutoffStart(void)
{
    static uint32_t prevStart = 0xFFFFFFFF;
    static uint32_t prevEnd   = 0xFFFFFFFF;
    static uint8_t  prevBlink = 0xFF;




    if (!CurrentDisplayStatus) {
        PracticeCutoff_DrawCommonFrame_NoRefresh("START FREQ", g_PracticeCutoff_PreserveLine2_OnNextEntry);

        Practice_RegisterChars_ForMode(CutOffOnOff);
        Practice_DrawPlayPauseIcon_IfAllowed(); // (2,16)을 상태에 맞게 즉시 보정


        // 🔧 전환 직후 잔상 제거: END 4칸(열 8~11)을 '무조건' 숫자로 그려서 공백 보존 제거
        {
            char e[5]; snprintf(e, sizeof(e), "%4d", (int)CutOffFreqEnd);
            LCD16X2_Set_Cursor(MyLCD, 2, 8);
            LCD16X2_Write_String(MyLCD, e);
        }

        CurrentDisplayStatus = 1;
        prevStart = 0xFFFFFFFF;
        prevEnd   = CutOffFreqEnd;       // 방금 숫자로 그렸으니 동기화
        prevBlink = UITotalBlinkStatus;  // 엔트리 프레임에 맞춰 동기화
        g_PracticeCutoff_PreserveLine2_OnNextEntry = 0;
    }




    // 로터리: Start ±20 (Start ≤ End, 하한 20)
    // 로터리: Start 가변 스텝 (Start ≤ End), 스텝: <1000 → 20 / <2000 → 50 / ≥2000 → 100
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        uint32_t step = (CutOffFreqStart >= 2000U) ? 100U : (CutOffFreqStart >= 1000U ? 50U : 20U);
        uint32_t newStart = CutOffFreqStart + step;
        if (newStart > CutOffFreqEnd) newStart = CutOffFreqEnd; // Start는 End를 넘지 않음
        if (newStart > 9900U) newStart = 9900U;
        CutOffFreqStart = newStart;
        rotaryEvent3 = ROTARY_EVENT_NONE;

        // 클릭음: 현재 Start(20..9900) → 0..1 정규화 → 700..2000Hz 로그 보간
        {
            float t = (CutOffFreqStart <= 20U) ? 0.0f :
                      (CutOffFreqStart >= 9900U) ? 1.0f :
                      ((float)(CutOffFreqStart - 20U) / 9880.0f);
            float click = 700.0f * powf(2000.0f / 700.0f, t);
            notch_ui_rotary_click_freq(click);
        }

    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        uint32_t step = (CutOffFreqStart >= 2000U) ? 100U : (CutOffFreqStart >= 1000U ? 50U : 20U);
        uint32_t newStart = (CutOffFreqStart > step) ? (CutOffFreqStart - step) : 20U;
        if (newStart < 20U) newStart = 20U;
        CutOffFreqStart = newStart;
        // Start가 End보다 커졌다면 End 쪽은 그대로 두고, Start만 유지(표현상 자연스러움)
        if (CutOffFreqStart > CutOffFreqEnd) CutOffFreqStart = CutOffFreqEnd;

        rotaryEvent3 = ROTARY_EVENT_NONE;

        // 클릭음: 현재 Start 기반
        {
            float t = (CutOffFreqStart <= 20U) ? 0.0f :
                      (CutOffFreqStart >= 9900U) ? 1.0f :
                      ((float)(CutOffFreqStart - 20U) / 9880.0f);
            float click = 700.0f * powf(2000.0f / 700.0f, t);
            notch_ui_rotary_click_freq(click);
        }
    }





    // Start 4칸(열 3~6)만 깜빡
    if (prevStart != CutOffFreqStart || prevBlink != UITotalBlinkStatus) {
        LCD16X2_Set_Cursor(MyLCD, 2, 3);
        if (UITotalBlinkStatus) {
            LCD16X2_Write_String(MyLCD, "    ");
        } else {
            char s[5]; snprintf(s, sizeof(s), "%4d", (int)CutOffFreqStart);
            LCD16X2_Write_String(MyLCD, s);
        }
        prevStart = CutOffFreqStart;
        prevBlink = UITotalBlinkStatus;

        // ▼▼▼ AFTER you commit new CutOffFreqStart ▼▼▼
        if (CurrentInstrumentType == Instrument_USER_PRESET_1) {
            CutOffFreqStartUser1 = CutOffFreqStart;
        } else if (CurrentInstrumentType == Instrument_USER_PRESET_2) {
            CutOffFreqStartUser2 = CutOffFreqStart;
        } else if (CurrentInstrumentType == Instrument_USER_PRESET_3) {
            CutOffFreqStartUser3 = CutOffFreqStart;
        }


    }

    // 버튼: short → END (2행 보존), long → MODE
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus = 0;
        g_PracticeCutoff_PreserveLine2_OnNextEntry = 1; // 2행 그대로 둔 채로 제목만 교체
        notch_ui_button_beep();

        currentUIState = UI_PRACTICE_FREQ_SETTING_CUTOFF_END;
        return;
    }
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus      = 0;
        PracticeHomeFirstRunFlag  = 0;
        g_PracticeCutoff_PreserveLine2_OnNextEntry = 0;
        currentUIState            = UI_MODE_SELECTION;

        notch_ui_mode_return_triple_beep();

        return;

    }

    if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS) buttonEvents[0] = BUTTON_EVENT_NONE;
}

void PracticeFreqSettingCutoffEnd(void)
{
    static uint32_t prevStart = 0xFFFFFFFF;
    static uint32_t prevEnd   = 0xFFFFFFFF;
    static uint8_t  prevBlink = 0xFF;

    if (!CurrentDisplayStatus) {
        PracticeCutoff_DrawCommonFrame_NoRefresh("END FREQ", g_PracticeCutoff_PreserveLine2_OnNextEntry);

        Practice_RegisterChars_ForMode(CutOffOnOff);
        Practice_DrawPlayPauseIcon_IfAllowed(); // (2,16)을 상태에 맞게 즉시 보정

        // 🔧 전환 직후 잔상 제거: START 4칸(열 3~6)을 '무조건' 숫자로 그려서 공백 보존 제거
        {
            char s[5]; snprintf(s, sizeof(s), "%4d", (int)CutOffFreqStart);
            LCD16X2_Set_Cursor(MyLCD, 2, 3);
            LCD16X2_Write_String(MyLCD, s);
        }

        CurrentDisplayStatus = 1;
        prevStart = CutOffFreqStart;     // 방금 숫자로 그렸으니 동기화
        prevEnd   = 0xFFFFFFFF;
        prevBlink = UITotalBlinkStatus;  // 엔트리 프레임에 맞춰 동기화
        g_PracticeCutoff_PreserveLine2_OnNextEntry = 0;
    }

    // 로터리: End ±20 (End ≥ Start)
    // 로터리: End 가변 스텝 (End ≥ Start), 스텝: <1000 → 20 / <2000 → 50 / ≥2000 → 100
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        uint32_t step = (CutOffFreqEnd >= 2000U) ? 100U : (CutOffFreqEnd >= 1000U ? 50U : 20U);
        uint32_t newEnd = CutOffFreqEnd + step;
        if (newEnd > 9900U) newEnd = 9900U;
        if (newEnd < CutOffFreqStart) newEnd = CutOffFreqStart;
        CutOffFreqEnd = newEnd;
        rotaryEvent3 = ROTARY_EVENT_NONE;

        // 클릭음: 현재 End(20..9900) → 0..1 정규화 → 700..2000Hz 로그 보간
        {
            float t = (CutOffFreqEnd <= 20U) ? 0.0f :
                      (CutOffFreqEnd >= 9900U) ? 1.0f :
                      ((float)(CutOffFreqEnd - 20U) / 9880.0f);
            float click = 700.0f * powf(2000.0f / 700.0f, t);
            notch_ui_rotary_click_freq(click);
        }

    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        uint32_t step = (CutOffFreqEnd >= 2000U) ? 100U : (CutOffFreqEnd >= 1000U ? 50U : 20U);
        uint32_t newEnd = (CutOffFreqEnd > step) ? (CutOffFreqEnd - step) : 20U;
        if (newEnd < CutOffFreqStart) newEnd = CutOffFreqStart;
        if (newEnd < 20U) newEnd = 20U;
        CutOffFreqEnd = newEnd;
        rotaryEvent3 = ROTARY_EVENT_NONE;

        // 클릭음: 현재 End(20..9900) 기반
        {
            float t = (CutOffFreqEnd <= 20U) ? 0.0f :
                      (CutOffFreqEnd >= 9900U) ? 1.0f :
                      ((float)(CutOffFreqEnd - 20U) / 9880.0f);
            float click = 700.0f * powf(2000.0f / 700.0f, t);
            notch_ui_rotary_click_freq(click);
        }
    }

    // End 4칸(열 8~11)만 깜빡
    if (prevEnd != CutOffFreqEnd || prevBlink != UITotalBlinkStatus) {
        LCD16X2_Set_Cursor(MyLCD, 2, 8);
        if (UITotalBlinkStatus) {
            LCD16X2_Write_String(MyLCD, "    ");
        } else {
            char e[5]; snprintf(e, sizeof(e), "%4d", (int)CutOffFreqEnd);
            LCD16X2_Write_String(MyLCD, e);
        }
        prevEnd   = CutOffFreqEnd;
        prevBlink = UITotalBlinkStatus;

        // ▼▼▼ AFTER you commit new CutOffFreqEnd ▼▼▼
        if (CurrentInstrumentType == Instrument_USER_PRESET_1) {
            CutOffFreqEndUser1 = CutOffFreqEnd;
        } else if (CurrentInstrumentType == Instrument_USER_PRESET_2) {
            CutOffFreqEndUser2 = CutOffFreqEnd;
        } else if (CurrentInstrumentType == Instrument_USER_PRESET_3) {
            CutOffFreqEndUser3 = CutOffFreqEnd;
        }


    }

    // 버튼: short → PRACTICE 홈, long → MODE
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus      = 0;
        PracticeHomeFirstRunFlag  = 0;

        // ★ Notch 모드에서 End Freq 설정을 마치고 돌아갈 때, 다음 진입 1회 토스트 스킵
        if (CutOffOnOff == 1) g_SkipNotchToastOnce = 1;

        g_PracticeCutoff_PreserveLine2_OnNextEntry = 0;
        currentUIState            = UI_PRACTICE_HOME;
        return;
        // 버튼 short-press 확정 시
        notch_ui_button_beep();


    }
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus      = 0;
        PracticeHomeFirstRunFlag  = 0;
        g_PracticeCutoff_PreserveLine2_OnNextEntry = 0;

        notch_ui_mode_return_triple_beep();
        currentUIState            = UI_MODE_SELECTION;

        return;
    }

    if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS) buttonEvents[0] = BUTTON_EVENT_NONE;
}




void PracticeInstPresetSetting(void) {
// 미사용
}



/* 튜너 화면일 때만 DSP 튜너를 켜고, 아니면 끄는 헬퍼 */
// main.c 안에 있던 기존 함수 수정
void TunerUI_PumpFromMain(void)
{
    uint8_t in_tuner =
        (currentUIState == UI_TUNER_HOME) ||
        (currentUIState == UI_TUNER_MODE_INTRO) ||
        (currentUIState == UI_TUNER_BASE_A_FREQ_SETTING);

    // ★ LED가 튜너 모드일 때도 튜너를 돌리게 한다
    extern volatile uint8_t g_led_mode_sel;
    if (g_led_mode_sel == 6) {   // LEDcontrol.c에서 튜너 패턴이 case 6이었지
        in_tuner = 1;
    }

    g_tuner_ui_on = in_tuner;
    notch_set_tuner_enabled(in_tuner);
}



void TunerIntro(void) {

}


#define NOTE_A4_INDEX   48
#define NOTE_COUNT      (sizeof(NoteNames)/sizeof(NoteNames[0]))

static inline float _log2f_safe(float x){ return logf(x) / 0.69314718056f; } // ln2



// fHz → (noteIndex, cents). 0Hz 또는 유효하지 않으면 noteIndex=0xFF로 리턴
void Tuner_FreqToNote(float fHz, uint16_t A_ref, uint8_t* outNoteIndex, int16_t* outCents)
{
    if (!outNoteIndex || !outCents) return;

    if (fHz <= 0.0f || A_ref < 200 || A_ref > 500) { // 대충 방어
        *outNoteIndex = 0xFF;
        *outCents     = 0;
        return;
    }

    // 이상적인 음높이 인덱스(연속값)
    float n_cont = 12.0f * _log2f_safe(fHz / (float)A_ref) + (float)NOTE_A4_INDEX;

    // 가장 가까운 반음으로 반올림
    int idx = (int)lrintf(n_cont);
    if (idx < 0) idx = 0;
    if (idx >= (int)NOTE_COUNT) idx = (int)NOTE_COUNT - 1;

    // 그 반음의 기준 주파수
    float f_note = (float)A_ref * powf(2.0f, ((float)idx - (float)NOTE_A4_INDEX)/12.0f);

    // fHz가 해당 반음에서 얼마나 벗어났는지(센트)
    int cent = (int)lrintf(1200.0f * _log2f_safe(fHz / f_note));
    // 반올림했으니 보통 [-50, +50]에 들어옴. 과도치 컷
    if (cent < -600) cent = -600;
    if (cent >  600) cent =  600;

    *outNoteIndex = (uint8_t)idx;
    *outCents     = (int16_t)cent;
}

// (noteIndex, cents) → fHz
float Tuner_NoteToFreq(uint8_t noteIndex, int16_t cents, uint16_t A_ref)
{
    if (noteIndex >= NOTE_COUNT || A_ref < 200 || A_ref > 500) return 0.0f;
    float f_note = (float)A_ref * powf(2.0f, ((float)noteIndex - (float)NOTE_A4_INDEX)/12.0f);
    return f_note * powf(2.0f, (float)cents / 1200.0f);
}


extern volatile int32_t TunerMeasurement_x10;
static inline float Tuner_GetLatestHz(void){
  int32_t q = TunerMeasurement_x10;   // 원자 32-bit
  return (float)q * 0.1f;
}






/* --- TUNER HOME --- */
void TunerHome(void)
{
	static uint8_t  s_sens_toast_drawn = 0;  // 토스트를 이번 토스트 윈도우에서 이미 그렸는지

    // ── 1회 초기화 ─────────────────────────────────────────────
    if (!TunerHomeFirstRunFlag) {

    	TunerMeasurement = 0;

        LCDColorSet(2); // 대기색
        LCD16X2_Clear(MyLCD);
        LCD16X2_ClearCustomChars(0);

        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "TUNER");

        LCD16X2_Set_Cursor(MyLCD, 1, 12);
        char abuf[6]; sprintf(abuf, "%3d", TunerBaseFreq);
        LCD16X2_Write_String(MyLCD, abuf);
        LCD16X2_Write_String(MyLCD, "Hz");



        notch_tuner_set_fs_trim_ppm(TunerCalibrationValue); // 시작값(대강) , 튜너 보정용
        notch_set_tuner_enabled(1);

        // 2행은 동적으로 한 줄 전체를 한 번에 그려서 플리커 방지
        TunerHomeFirstRunFlag = 1;
    }

    // ── 필터(EMA) ──────────────────────────────────────────────
    static uint8_t  s_ema_inited = 0;
    static float    s_ema_hz     = 0.0f;
    static uint32_t s_last_tick  = 0;

    float rawHz = Tuner_GetLatestHz();
    uint32_t now = HAL_GetTick();

    if (!s_ema_inited) {
        s_ema_hz = rawHz;
        s_ema_inited = 1;
        s_last_tick = now;
    } else {
        float dt = (float)(now - s_last_tick); if (dt < 1) dt = 1;
        float tau = 20.0f;                      // ~0.4s 반응 (저가형 튜너 느낌)
        float a = dt / (tau + dt);               // 0.1
        s_ema_hz += a * (rawHz - s_ema_hz);
        s_last_tick = now;
    }

    // ── 값 홀드: 무음 들어와도 잠깐 지난 값으로 버티기 ──
    static float    s_hold_hz = 0.0f;
    static uint32_t s_hold_ms = 0;
    float dispHz;

    if (rawHz > 0.5f) {
        // 이번 프레임은 실제로 값이 있음 → 이걸 새 기준으로 저장
        s_hold_hz = s_ema_hz;
        s_hold_ms = now;
        dispHz    = s_ema_hz;
    } else if ((now - s_hold_ms) < 250u) {
        // 값은 안 들어왔는데 방금 전까지는 있었다 → 그걸로 250ms만 버팀
        dispHz = s_hold_hz;
    } else {
        // 진짜로 오래 무음이면 0으로
        dispHz = 0.0f;
    }


    // ── 무음 처리 ──────────────────────────────────────────────
    uint8_t noteIdx;
    int16_t cents;
    Tuner_FreqToNote(dispHz, TunerBaseFreq, &noteIdx, &cents);


    // 화면 캐시(바뀔 때만 다시 그림)
    static uint8_t  prev_valid      = 0;     // 0=무음/무효, 1=유효
    static uint8_t  prev_noteIdx    = 0xFF;
    static int16_t  prev_cents      = 32767;
    static uint8_t  prev_arrowLen   = 255;   // 0~3
    static int8_t   prev_arrowDir   = 99;    // -1(Flat) / 0(In-tune) / +1(Sharp)
    static uint8_t  prev_color      = 0xFF;
    static char     prev_line[17]   = {0};

    // 안정성 게이팅(센트 히스토리)
    static int16_t  s_cent_hist[6];
    static uint8_t  s_hist_cnt = 0, s_hist_pos = 0;
    static uint8_t  s_hist_init = 0;
    static uint32_t s_stable_since = 0;
    static uint8_t  s_is_stable = 0;


    // [TOP-LEVEL TOAST] draw-once & clear-once (UI보다 우선)
    if (HAL_GetTick() < s_tnr_sens_toast_until_ms) {
        if (!s_sens_toast_drawn) {
            static const char* kSensName[3] = { "LOW", "MID", "HIGH" };
            LCDColorSet(2);
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            char line[17]; snprintf(line, sizeof(line), "SENS: %-4s      ", kSensName[g_tnr_sens]);
            LCD16X2_Write_String(MyLCD, line);
            s_sens_toast_drawn = 1;
        }
        return;   // 토스트 기간엔 다른 UI 그리지 않음
    } else if (s_sens_toast_drawn) {
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, "                "); // clear-once
        s_sens_toast_drawn = 0;
        prev_valid = 0; // 다음 프레임 정상 UI가 한 번만 그려지도록
        // return; // 선택사항
    }



    // 무음(값 무효) → 대기 모드 표시(바뀔 때만)
    if (noteIdx == 0xFF) {
            LCDColorSet(2);

            LCD16X2_Set_Cursor(MyLCD, 2, 1);

            if (g_tnr_overload) {
                // 16칸 가운데 정렬: "OVERLOAD!"
                LCD16X2_Write_String(MyLCD, "   OVERLOAD!    ");
            } else {
                // 기존 동작 유지
                if (UITotalBlinkStatus == 1) {
                    LCD16X2_Write_String(MyLCD, "      ----      ");
                } else {
                    LCD16X2_Write_String(MyLCD, "                ");
                }
            }

            prev_valid = 0;
            // 안정성 상태 리셋
            s_hist_init = 0; s_is_stable = 0; s_hist_cnt = 0; s_hist_pos = 0;
            CurrnetTunerNote = 0;
            CurrentTunerCent = 0;

            // [MODIFY][TUNER] SENS 토스트: draw-once & clear-once
            if (HAL_GetTick() < s_tnr_sens_toast_until_ms) {
                if (!s_sens_toast_drawn) {                // 아직 안 그렸으면 이번에 한 번만 그림
                    static const char* kSensName[3] = { "LOW", "MID", "HIGH" };
                    LCDColorSet(2);                        // 대기색 유지
                    LCD16X2_Set_Cursor(MyLCD, 2, 1);
                    char line[17]; snprintf(line, sizeof(line), "SENS: %-4s      ", kSensName[g_tnr_sens]);
                    LCD16X2_Write_String(MyLCD, line);
                    s_sens_toast_drawn = 1;
                }
                return;                                   // 토스트 기간에는 추가로 그리지 않음
            } else if (s_sens_toast_drawn) {
                // 토스트 막 끝난 시점: 2행을 한 번만 지우고(혹은 무효화) 정상 UI로 복귀
                LCD16X2_Set_Cursor(MyLCD, 2, 1);
                LCD16X2_Write_String(MyLCD, "                ");   // clear-once
                s_sens_toast_drawn = 0;

                // 다음 주기에 정상 UI가 깔끔히 다시 그려지도록 캐시 무효화
                prev_valid = 0;                                     // [중요] 한 프레임 뒤 정상 UI가 한 번만 그려짐
                // 여기서 return 하지 말고 아래 정상 렌더링으로 흘려도 됨(원하면 return; 해도 무방)
            }


        goto buttons;


    }

    // 유효 입력: 히스토리 준비/업데이트
    if (!s_hist_init) {
        for (int i = 0; i < 6; ++i) s_cent_hist[i] = cents;
        s_hist_cnt = 1; s_hist_pos = 0;
        s_stable_since = now;
        s_is_stable = 0;
        s_hist_init = 1;
    } else {
        s_cent_hist[s_hist_pos] = cents;
        s_hist_pos = (uint8_t)((s_hist_pos + 1) % 6);
        if (s_hist_cnt < 6) s_hist_cnt++;
    }

    // 최근 6샘플 범위
    int16_t cmin = s_cent_hist[0], cmax = s_cent_hist[0];
    for (uint8_t i = 1; i < s_hist_cnt; ++i) {
        if (s_cent_hist[i] < cmin) cmin = s_cent_hist[i];
        if (s_cent_hist[i] > cmax) cmax = s_cent_hist[i];
    }
    int16_t crange = (int16_t)(cmax - cmin);
    if (crange <= 6) { // 흔들림 거의 없음
        if (!s_is_stable && (now - s_stable_since) >= 250) s_is_stable = 1;
    } else {
        s_is_stable = 0;
        s_stable_since = now;
    }

    // 색상/화살표 버킷 계산
    //  - 색상: |cent|<=3 green(3), <=20 yellow(1), >20 red(5)
    //  - 화살표 길이: 0(<=3), 1(<=20), 2(<=35), 3(>35)  (최대 3개)
    // === relaxed judgement (더 여유 있는 판정) ===
    int a = abs((int)cents);

    // 색상: 초록 허용폭을 ±8센트로 확대, 노랑도 조금 넓힘
    uint8_t color = (a <= 3) ? 3 : (a <= 24 ? 1 : 5);

    // 화살표 길이: 구간을 완만하게(0:≤5, 1:≤16, 2:≤28, 3:>28)
    uint8_t arrowLen = (a <= 3)  ? 0
                     : (a <= 15) ? 1
                     : (a <= 20) ? 2 : 3;

    // 화살표 방향 dead-band도 ±8로 완화(중앙에서 덜 튐)
    int8_t  arrowDir = (cents >  3) ? +1
                     : (cents < -3) ? -1 : 0;
    // “안정 상태”에서만 화면 갱신. 그리고 바뀐게 있을 때만 갱신.
    //if (s_is_stable) {
        // --- build full 16-char line buffer (no direct LCD writes) ---
        char line[17];
        for (int i = 0; i < 16; ++i) line[i] = ' ';
        line[16] = '\0';

        // NOTE 문자열 중앙 배치
        char noteStr[6] = "    ";
        if (noteIdx < NOTE_COUNT) snprintf(noteStr, sizeof(noteStr), "%s", NoteNames[noteIdx]);
        int noteLen = (int)strlen(noteStr);
        if (noteLen > 5) noteLen = 5;
        int noteStart = 1 + (16 - noteLen) / 2; // 1..16 (1-based)
        for (int i = 0; i < noteLen && (noteStart - 1 + i) < 16; ++i) {
            line[(noteStart - 1) + i] = noteStr[i];
        }

        // --- arrow length mapping (너가 원한 매핑: 0:<=5, 1:6-10, 2:11-20, 3:21-30, 4:>30) ---
        int absCent = abs((int)cents);
        uint8_t computedArrowLen;
        if (absCent <= 5) computedArrowLen = 0;
        else if (absCent <= 10) computedArrowLen = 1;
        else if (absCent <= 20) computedArrowLen = 2;
        else if (absCent <= 30) computedArrowLen = 3;
        else computedArrowLen = 4;

        // arrow direction: + = sharp (display on right side '<'), - = flat (display on left side '>')
        int8_t computedArrowDir = (cents > 5) ? +1 : ((cents < -5) ? -1 : 0);

        // Fixed arrow zones per 너의 요구:
        // left zone positions 2..5  -> indices 1..4
        // right zone positions 12..15 -> indices 11..14
        if (computedArrowLen > 0) {
            if (computedArrowDir > 0) {
                // Sharp -> place '<' at right zone, left-to-right
                for (int i = 0; i < (int)computedArrowLen && i < 4; ++i) {
                    int idx = 11 + i; // 12..15 -> indices 11..14
                    if (idx >= 0 && idx < 16) line[idx] = '<';
                }
            } else if (computedArrowDir < 0) {
                // Flat -> place '>' at left zone, right-to-left (so arrow "points" inward)
                for (int i = 0; i < (int)computedArrowLen && i < 4; ++i) {
                    int idx = 4 - i; // 5..2 -> indices 4..1
                    if (idx >= 0 && idx < 16) line[idx] = '>';
                }
            }
        }

        // 색상 결정 (기존 로직 재사용)
       // uint8_t color = (a <= 5) ? 3 : (a <= 20 ? 1 : 5);

        // --- 이제 한 번만 LCD에 쓰기: 바뀐 경우에만 ---
        uint8_t need_draw = 0;
        if (!prev_valid || prev_noteIdx != noteIdx || prev_arrowLen != computedArrowLen ||
            prev_arrowDir != computedArrowDir || strcmp(prev_line, line) != 0) {
            need_draw = 1;
        }

        if (need_draw) {
            // 색상 변경은 write 전에 해도 되고 후에 해도 됨 — 변화가 적게 일어나도록 guard 완료
            if (color != prev_color) {
                LCDColorSet(color);
                prev_color = color;
            }
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, line);
            strcpy(prev_line, line);
            prev_valid    = 1;
            prev_noteIdx  = noteIdx;
            prev_cents    = cents;
            prev_arrowLen = computedArrowLen;
            prev_arrowDir = computedArrowDir;
        }

        // 전역 보고
        CurrnetTunerNote = (uint16_t)noteIdx;
        CurrentTunerCent = (uint16_t)cents;



        // 캘리브레이션에 따라 색 변경
        if (color != prev_color) {
            LCDColorSet(color);
            prev_color = color;
        }

        // 내용이 바뀐 경우에만 LCD 쓰기
        if (!prev_valid || prev_noteIdx != noteIdx || prev_arrowLen != arrowLen ||
            prev_arrowDir != arrowDir || strcmp(prev_line, line) != 0) {
            need_draw = 1;
        }
        if (need_draw) {
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, line);
            strcpy(prev_line, line);
            prev_valid    = 1;
            prev_noteIdx  = noteIdx;
            prev_cents    = cents;
            prev_arrowLen = arrowLen;
            prev_arrowDir = arrowDir;
        }

        // 전역 보고
        CurrnetTunerNote = (uint16_t)noteIdx;
        CurrentTunerCent = (uint16_t)cents;
    //}
    //else {
        // 불안정 구간: 대기색 + 기존 표시 유지(추가 깜빡임 방지).
  //      if (prev_color != 2) { LCDColorSet(2); prev_color = 2; }
   //     prev_valid = 1; // “유효 음을 듣는 중” 상태
  //  }

buttons:
	// [ADD][TUNER] 버튼 0: Short Press → SENS 순환 + 토스트 800ms
	if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {

		g_tnr_sens = (uint8_t)((g_tnr_sens + 1) % 3);          // LOW→MID→HIGH→LOW...
		s_sens_toast_drawn = 0;                                 // draw-once 리셋
		s_tnr_sens_toast_until_ms = HAL_GetTick() + 800;        // 0.8s 토스트
        notch_ui_button_beep();
		buttonEvents[0] = BUTTON_EVENT_NONE;

        // **즉시 한 번 그려주고 리턴** → 지금 프레임부터 보이게
        static const char* kSensName[3] = { "LOW", "MID", "HIGH" };
        LCDColorSet(2);
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        char line[17]; snprintf(line, sizeof(line), "SENS: %-4s      ", kSensName[g_tnr_sens]);
        LCD16X2_Write_String(MyLCD, line);
        s_sens_toast_drawn = 1;
        return;

	}

    // 버튼 로직(그대로)
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2]           = BUTTON_EVENT_NONE;
        TunerHomeFirstRunFlag     = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_button_beep();

        currentUIState            = UI_TUNER_BASE_A_FREQ_SETTING;
        return;
    }
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2]           = BUTTON_EVENT_NONE;
        TunerHomeFirstRunFlag     = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_button_beep();

        currentUIState            = UI_MODE_SELECTION;
        return;
    }


}



void TunerBaseAFreqSetting(void) {
    static uint16_t prevFreq = 65535;
    static uint8_t  prevBlink = 0xFF;   // ✨ 깜빡이 상태 캐시 추가

    if (!CurrentDisplayStatus) {

        LCDColorSet(2);

        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "A4 REF");
        LCD16X2_Set_Cursor(MyLCD, 2, 11);
        LCD16X2_Write_String(MyLCD, "           ");

        LCD16X2_Set_Cursor(MyLCD, 1, 15);
        LCD16X2_Write_String(MyLCD, "Hz");
        CurrentDisplayStatus = 1;

        prevFreq  = 65535;   // 강제 출력 유도
        prevBlink = 0xFF;    // ✨ 깜빡이 강제 반영
    }

    // 🌀 rotary 조절
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        if (TunerBaseFreq < 480) {  // 예: 최대 500Hz 제한
            TunerBaseFreq += 1;
        }

        notch_ui_rotary_click_freq( 1200.f );
        rotaryEvent3 = ROTARY_EVENT_NONE;
    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        if (TunerBaseFreq > 400) {  // 예: 최소 300Hz 제한
            TunerBaseFreq -= 1;
        }

        notch_ui_rotary_click_freq( 800.f );
        rotaryEvent3 = ROTARY_EVENT_NONE;
    }

    // 🖥️ 값 출력 (✨ 숫자만 깜빡이)
    if (TunerBaseFreq != prevFreq || prevBlink != UITotalBlinkStatus) {
        LCD16X2_Set_Cursor(MyLCD, 1, 12);  // A= 뒤에 출력 (숫자 위치 고정)
        if (UITotalBlinkStatus) {
            LCD16X2_Write_String(MyLCD, "   ");  // ✨ 깜빡일 때 숫자 자리만 공백(3칸)
        } else {
            char buf[6];
            sprintf(buf, "%3d", TunerBaseFreq);
            LCD16X2_Write_String(MyLCD, buf);
        }
        prevFreq  = TunerBaseFreq;
        prevBlink = UITotalBlinkStatus;
    }

    // 🎯 short → TUNER HOME으로 복귀
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus = 0;

        notch_ui_button_beep();

        currentUIState = UI_TUNER_HOME;
        return;
    }

    // 🚪 long → 메뉴로 복귀
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }
}


////////////////////////////////

void MetronomeModeIntro(void) {
    static uint8_t first = 0;
    if (!first) {
        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "METRONOME");
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_ScrollTextDelay(
            MyLCD,
            "Metronome Mode. Press VOL Dial to start/stop.",
            150, 0, 2, 1
        );
        first = 1;
    }
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }
}

// 홈/설정 공통 셸을 그려주는 헬퍼
void Metronome_DrawCommonFrame(uint8_t full_redraw)
{
    static uint8_t s_prevReady    = 0xFF;

    if (full_redraw) {
        LCDColorSet(2);

        LCD16X2_ClearCustomChars(0);
        LCD16X2_RegisterCustomChar(0, 0, Playing);
        LCD16X2_RegisterCustomChar(0, 1, Paused);
        LCD16X2_RegisterCustomChar(0, 2, MelodyIcon);
        LCD16X2_RegisterCustomChar(0, 3, Timer);   // 안 써도 일단 등록은 해두자

        LCD16X2_Clear(MyLCD);

        // 1행 헤더
        //LCD16X2_Set_Cursor(MyLCD, 1, 1);
        //LCD16X2_Write_String(MyLCD, "METRONOME");

        // =========================
        // 2행 기본 틀
        // (1) 음표
        LCD16X2_DisplayCustomChar(0, 2, 1, 2);
        // (2) 빈칸
        LCD16X2_Set_Cursor(MyLCD, 2, 2);
        LCD16X2_Write_String(MyLCD, " ");

        // (3~5) BPM 3자리 자리만 비워둠
        // 실제 숫자는 BPM 화면/설정 화면에서 채운다

        // (6~8) "BPM"
        LCD16X2_Set_Cursor(MyLCD, 2, 6);
        LCD16X2_Write_String(MyLCD, "BPM");

        // (9) 구분용 공백
        LCD16X2_Set_Cursor(MyLCD, 2, 9);
        LCD16X2_Write_String(MyLCD, " ");

        // (10~11) 분자 자리 - 값은 각 화면에서
        // (12) '/'
        LCD16X2_Set_Cursor(MyLCD, 2, 12);
        LCD16X2_Write_String(MyLCD, "/");
        // (13~14) 분모 자리 - 값은 각 화면에서

        // (15) 그냥 공백 하나 깔아두기 (16은 재생/일시정지 아이콘이 씀)
        LCD16X2_Set_Cursor(MyLCD, 2, 15);
        LCD16X2_Write_String(MyLCD, " ");

    }

}

// 2,16 재생/일시정지 아이콘만 관리하는 아주 작은 함수
void Metronome_UpdatePlayIcon(void)
{
    static uint8_t s_prev = 0xFF;

    if (s_prev != IsMetronomeReady) {
        // IsMetronomeReady == 1 → 재생(0), 아니면 일시정지(1)
        LCD16X2_DisplayCustomChar(0, 2, 16, IsMetronomeReady ? 0 : 1);
        s_prev = IsMetronomeReady;
    }
}


// ─────────────────────────────
// 메트로놈 서브비트/패턴 테이블
// ─────────────────────────────
typedef struct {
    char    name[10];   // 9글자 + 널
    uint8_t steps;     // 1..8
    uint8_t mask;      // 하위 8비트 사용, 1이면 그 칸에서 서브비트 낸다
} MetPatternDesc;

// ⚠️ 여기만 고치면 패턴 추가됨
static const MetPatternDesc s_metPatterns[] = {
    // 이름   steps mask
    { "DIV OFF  ", 1, 0x01 },       // 0: 완전 OFF (메인박만)
    { "DIVIDE 2 ", 2, 0x03 },       // 1: 2분할 (1,2)
    { "DIVIDE 3 ", 3, 0x07 },       // 2: 3분할 (1,2,3)
    { "DIVIDE 4 ", 4, 0x0F },       // 3: 4분할
    { "DIVIDE 5 ", 5, 0x1F },       // 4: 5분할
    { "DIVIDE 6 ", 6, 0x3F },       // 5: 6분할
    { "DIVIDE 7 ", 7, 0x7F },       // 6: 7분할
    { "DIVIDE 8 ", 8, 0xFF },       // 7: 8분할

    // 스윙/홀수 계열
    { "SWING 2-1", 3, 0b00000101 }, // 8: 트리플릿 스윙 (1, 3칸째) → 딴…따
    { "SWG 3-1  ", 4, 0b00001001 }, // 9: 3+1 → 긴셋 + 쇼트
    { "SWG 2-1-2", 5, 0b00010101 }, // 10: 2,1,2 → 짠-따-짠

    // 그루브 분할
    { "GROOV 3-2", 5, 0b00001001 }, // 11: 3+2 → 시작, 4번째 칸
    { "GRV 3-3-2", 8, 0b01001001 }, // 12: 라틴/폴리리듬 3-3-2 (칸0,3,6)
    { "GRV 2-2-3", 7, 0b00010101 }, // 13: 2-2-3 → 칸0,2,4
    { "GRV 5-3  ", 8, 0b00100001 }, // 14: 5+3 → 칸0,5
    { "GRV 7-1  ", 8, 0b10000001 }, // 15: 7+1 → 칸0,7
    { "GRV 4-1  ", 5, 0b00010001 }, // 16: 4+1 → 칸0,4
    { "GRV 1-3-1", 5, 0b00010011 }, // 17: 1,3,1 → 칸0,1,4
    { "GRV 2-3-2", 7, 0b00100101 }, // 18: 2,3,2 → 칸0,2,5
    { "GRV 3-2-3", 8, 0b00101001 }, // 19: 3,2,3 → 칸0,3,5
};

#define MET_PATTERN_COUNT (sizeof(s_metPatterns)/sizeof(s_metPatterns[0]))

static uint8_t g_metPatternIndex = 0;   // 현재 선택된 패턴

// 엔진이 보는 최종 값
volatile uint8_t g_metSubSteps = 1;  // 1이면 서브비트 없음
volatile uint8_t g_metSubMask  = 0x01;


// 패턴 인덱스 → 실제 엔진 파라미터로 반영
static void Metronome_ApplyPattern(uint8_t patIdx)
{
    if (patIdx >= MET_PATTERN_COUNT)
        patIdx = 0;

    g_metSubSteps = s_metPatterns[patIdx].steps;
    g_metSubMask  = s_metPatterns[patIdx].mask;
}



// 1행에 "PTN:xxxxx" 찍는 전용 함수
static void Metronome_DrawPatternHeader(uint8_t force)
{
    static uint8_t s_prevIdx = 0xFF;

    if (!force && s_prevIdx == g_metPatternIndex) {
        // 변경 없으면 안 그린다
        return;
    }
    // 패턴 이름 9글자
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, s_metPatterns[g_metPatternIndex].name);
    s_prevIdx = g_metPatternIndex;
}





/* --- METRONOME HOME --- */
void MetronomeHome(void) {

    static uint8_t s_prevRitOn = 0;   // [NEW] 직전 프레임에 RIT 오버레이가 켜져 있었는지
    static uint16_t s_prevBPM  = 0xFFFF;   // [NEW] 직전 표시 BPM
    static uint8_t  s_prevRit  = 0xFF;     // [NEW] 직전 RIT on/off

    if (!MetronomeHomeFirstRunFlag) {

        IsMetronomeReady = 0;

        // 공통 프레임 한 번만 초기화
        Metronome_DrawCommonFrame(1);
        Metronome_DrawPatternHeader(1);

        // 1행 12~16 카운터 초기표시 유지
        Met_DrawBeatField(0, 0, 1);



        // BPM 찍기 (2,3~5)
        {
            char bpmBuf[5];
            sprintf(bpmBuf, "%3d", MetronomeBPM);
            LCD16X2_Set_Cursor(MyLCD, 2, 3);
            LCD16X2_Write_String(MyLCD, bpmBuf);
        }

        // 분자 찍기 (2,10~11) — LEADING ZERO 없이
        {
            char numBuf[3];
            sprintf(numBuf, "%2d", TimeSignature);     // 4 -> " 4"
            LCD16X2_Set_Cursor(MyLCD, 2, 10);
            LCD16X2_Write_String(MyLCD, numBuf);
        }

        // 슬래시는 공통 프레임이 이미 2,12에 찍어줬음

        // 분모 찍기 (2,13~14)
        {
            char denBuf[3];
            sprintf(denBuf, "%2d", TimeSignatureDen);  // 4 -> " 4"
            LCD16X2_Set_Cursor(MyLCD, 2, 13);
            if (TimeSignatureDen < 10) {
                LCD16X2_Write_Char(MyLCD, '0' + TimeSignatureDen);
                LCD16X2_Write_String(MyLCD, " ");
            } else {
                char denBuf[3];
                sprintf(denBuf, "%2d", TimeSignatureDen);
                LCD16X2_Write_String(MyLCD, denBuf);
            }
        }

        MetronomeHomeFirstRunFlag = 1;
    }


    // 로터리로 패턴 변경 (재생 중엔 무시)
    if (IsMetronomeReady == 0) {
        if (rotaryEvent3 == ROTARY_EVENT_CW) {
            if (++g_metPatternIndex >= MET_PATTERN_COUNT)
                g_metPatternIndex = 0;
            rotaryEvent3 = ROTARY_EVENT_NONE;

            Metronome_ApplyPattern(g_metPatternIndex);
            Metronome_DrawPatternHeader(1);
            // 필요하면 클릭 사운드
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
            if (g_metPatternIndex == 0)
                g_metPatternIndex = MET_PATTERN_COUNT - 1;
            else
                g_metPatternIndex--;
            rotaryEvent3 = ROTARY_EVENT_NONE;

            Metronome_ApplyPattern(g_metPatternIndex);
            Metronome_DrawPatternHeader(1);
        }
    } else { /* IsMetronomeReady != 0 → 재생 중 로터리 동작 */
        if (rotaryEvent3 == ROTARY_EVENT_CCW) {         // 좌로 돌림
            rotaryEvent3 = ROTARY_EVENT_NONE;

            if (g_met_rit_mode == MET_RIT_MODE_RIT) {
                /* 리타 진행 중: 델타 증가(더 느려짐) */
                Met_Rit_DeltaUp();
            } else if (g_met_rit_mode == MET_RIT_MODE_ACCEL) {
                /* 아첼 진행 중: 델타 감소(덜 빨라짐) */
                Met_Rit_DeltaDown();
            } else {
                /* 진행 중 아님: 리타 시작(기본 delta=20 예시) */
                Met_Rit_StartDelta(0);
            }
        }
        else if (rotaryEvent3 == ROTARY_EVENT_CW) {     // 우로 돌림
            rotaryEvent3 = ROTARY_EVENT_NONE;

            if (g_met_rit_mode == MET_RIT_MODE_RIT) {
                /* 리타 진행 중: 델타 감소(덜 느려짐) */
                Met_Rit_DeltaDown();
            } else if (g_met_rit_mode == MET_RIT_MODE_ACCEL) {
                /* 아첼 진행 중: 델타 증가(더 빨라짐) */
                Met_Rit_DeltaUp();
            } else {
                /* 진행 중 아님: 아첼 시작(기본 delta=20 예시) */
                Met_Accel_StartDelta(0);
            }
        }
    }


    // ─────────────────────────────

    // SHORT PRESS → BPM 설정
    // ─────────────────────────────
    // SHORT PRESS → (재생 중이면 RIT 토글, 정지 중이면 BPM 설정 진입)
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;

        if (IsMetronomeReady) {
        	Met_Rit_ReturnToBase();
        } else {
            // 정지 중: 원래 기능(BPM 설정 화면 진입)
            currentUIState = UI_METRONOME_BPM_SETTING;
            // 필요하다면 다음에 홈으로 돌아올 때 풀 리드로우를 강제하고 싶을 때만 1로:
            // MetronomeHomeFirstRunFlag = 1;
        }

        notch_ui_button_beep();
        // 홈화면에서 불필요하게 전체 리드로우를 막고 싶으면 0 유지
        // MetronomeHomeFirstRunFlag = 0;
        return;
    }

    // LONG PRESS → 모드 선택
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        MetronomeHomeFirstRunFlag = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }

    if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[0] = BUTTON_EVENT_NONE;

        if (IsMetronomeReady == 1) {
            // 재생 중이면 그냥 끈다
            IsMetronomeReady = 0;
        } else {
            // 꺼져 있던 걸 켜는 순간
            // 만약 직전에 "다음 시작 때 리셋" 요청이 있었다면 여기서 리셋
            if (g_met_counter_reset_on_start) {
                g_met_counter_reset_on_start = 0;

                g_met_big_cnt   = 0;
                g_met_small_cnt = 0;
                //Met_DrawBeatField(g_met_big_cnt, g_met_small_cnt, 0);  // 1:1 찍기
            }

            IsMetronomeReady = 1;
        }

        notch_ui_button_beep();
        return;
    }

    if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[0] = BUTTON_EVENT_NONE;

        // 멈춰 있을 때만 초기화 예약
        if (IsMetronomeReady == 0) {
            // 화면을 대기 포맷으로만 바꾼다
            Met_DrawBeatField(0, 0, 1);          // ---:- 또는 ---:--
            // 그리고 "다음에 시작하면 1:1부터" 플래그 세팅
            g_met_counter_reset_on_start = 1;
        }

        notch_ui_button_beep();
        return;
    }


    /* [NEW] RIT 오버레이(1행 1열 9글자) + BPM 실시간 숫자 갱신 (firstrunflag 무시) */
    /* [NEW] 1행 1열 라벨: (a) ATEMPO 1초 토스트, (b) 델타/최종 BPM 9글자, (c) 기본 헤더 복귀 */
    {
        static uint8_t  s_prevShowedAtempo = 0;
        uint32_t now = HAL_GetTick();

        /* (a) a tempo 토스트가 유효하면 'ATEMPO' 1초 표시 (재생 중에도 OK) */
        if (s_atempo_until_ms && (int32_t)(now - s_atempo_until_ms) < 0) {
            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            /* 9칸 덮어쓰기 위해 뒤에 공백 3개 */
            LCD16X2_Write_String(MyLCD, "ATEMPO   ");
            s_prevShowedAtempo = 1;
        } else {
            if (s_prevShowedAtempo) {
                /* 토스트 막 끝났으면 헤더 복구 */
                Metronome_DrawPatternHeader(1);
                s_prevShowedAtempo = 0;
            }

            /* (b) 리타/아첼 진행 중이면 델타/최종 BPM 9글자: "+XX / XXX" */
            if (g_met_rit_mode == MET_RIT_MODE_RIT ||
                g_met_rit_mode == MET_RIT_MODE_ACCEL)
            {
                /* 앵커가 있으면 그걸 base로, 없으면 현재 BPM */
                uint16_t base = s_rit.base_bpm ? s_rit.base_bpm : MetronomeBPM;
                uint16_t targ = s_rit.targ_bpm ? s_rit.targ_bpm : MetronomeBPM;

                /* 부호 있는 델타: 최종 BPM - 기준 BPM */
                int32_t delta_signed = (int32_t)targ - (int32_t)base;
                if (delta_signed >  99) delta_signed =  99;
                if (delta_signed < -99) delta_signed = -99;

                /* "+XX / XXX" 포맷을 만들기 위한 버퍼 */
                char tmp[16];
                char lab[10];
                uint8_t i;

                /* lab을 공백 9칸 + NUL 로 초기화 */
                for (i = 0; i < 9; ++i) {
                    lab[i] = ' ';
                }
                lab[9] = '\0';

                /* 리딩제로 없이: %+d / %u  → 예) +5 / 120, -12 / 92 */
                snprintf(tmp, sizeof(tmp), "%+d / %u",
                         (int)delta_signed,
                         (unsigned)targ);

                /* 앞에서 최대 9글자만 복사해서 화면 덮어쓰기 */
                for (i = 0; i < 9 && tmp[i] != '\0'; ++i) {
                    lab[i] = tmp[i];
                }

                LCD16X2_Set_Cursor(MyLCD, 1, 1);
                LCD16X2_Write_String(MyLCD, lab);
            }
            else {
                /* (c) 진행 중이 아니면 헤더 유지 (아무 것도 안 함) */
            }
        }
    }

    /* BPM 숫자(2,3~5)는 값 변할 때마다 즉시 갱신 */
    {
        static uint16_t s_prevBPM = 65535;
        if (s_prevBPM != MetronomeBPM) {
            char buf[4];
            sprintf(buf, "%3d", MetronomeBPM);
            LCD16X2_Set_Cursor(MyLCD, 2, 3);
            LCD16X2_Write_String(MyLCD, buf);
            s_prevBPM = MetronomeBPM;
        }
    }

    Metronome_UpdatePlayIcon();
}


// 홈/설정 공통 셸을 그려주는 헬퍼


void MetronomeBPMSetting(void)
{
    static uint16_t prevBPM    = 65535;
    static uint8_t  prevBlink  = 0xFF;
    static uint8_t  prevSigNum = 255;
    static uint8_t  prevSigDen = 255;

    // 1회 진입 시: 공통 프레임 그리고 강제 리프레시 플래그 세팅
    if (!CurrentDisplayStatus) {

        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "SET BPM         ");

        LCD16X2_Set_Cursor(MyLCD, 2, 16);
        LCD16X2_Write_String(MyLCD, " ");

        prevBPM    = 65535;
        prevBlink  = 0xFF;
        prevSigNum = 255;
        prevSigDen = 255;

        CurrentDisplayStatus = 1;
    }

    // ───── 로터리로 BPM 1씩 증감 (30~300), 기존 코드 그대로 ─────
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        if (MetronomeBPM < 300) MetronomeBPM++;

        // 기존에 쓰던 그 “쭉 올라가는” 클릭 사운드
        notch_ui_rotary_click_freq(
            1000.0f * powf(4.0f, (MetronomeBPM - 30.0f) * (1.0f / 270.0f))
        );

        rotaryEvent3 = ROTARY_EVENT_NONE;
    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        if (MetronomeBPM > 30) MetronomeBPM--;

        notch_ui_rotary_click_freq(
            1000.0f * powf(4.0f, (MetronomeBPM - 30.0f) * (1.0f / 270.0f))
        );

        rotaryEvent3 = ROTARY_EVENT_NONE;
    }

    // 공통 프레임은 매 주기 호출 (재생/일시정지 아이콘 갱신)

    // ───── BPM 3자리 (2,3~5) + 깜빡임 유지 ─────
    if (prevBPM != MetronomeBPM || prevBlink != UITotalBlinkStatus) {
        LCD16X2_Set_Cursor(MyLCD, 2, 3);
        if (UITotalBlinkStatus) {
            LCD16X2_Write_String(MyLCD, "   ");   // 깜빡일 때는 공백
        } else {
            char buf[4];
            sprintf(buf, "%3d", MetronomeBPM);
            LCD16X2_Write_String(MyLCD, buf);
        }
        prevBPM   = MetronomeBPM;
        prevBlink = UITotalBlinkStatus;
    }

    // ───── Time Sig 분자/분모 표시 (네가 새로 정한 2행 레이아웃) ─────
    // 분자: (2,10~11), 10의 자리가 0이면 공백
    if (prevSigNum != TimeSignature) {
        uint8_t num = TimeSignature;
        char nb[3];
        sprintf(nb, "%2d", num);           // 4 -> " 4", 12 -> "12"
        LCD16X2_Set_Cursor(MyLCD, 2, 10);
        LCD16X2_Write_String(MyLCD, nb);
        prevSigNum = num;
    }

    // 슬래시는 공통 프레임이 2,12에 이미 넣어뒀으니까 건드리지 않아도 됨

    // 분모: (2,13~14), 9 이하일 땐 13열에만 찍고 14열은 공백
    if (prevSigDen != TimeSignatureDen) {
        uint8_t den = TimeSignatureDen;
        LCD16X2_Set_Cursor(MyLCD, 2, 13);
        if (den < 10) {
            LCD16X2_Write_Char(MyLCD, '0' + den);  // 13열
            LCD16X2_Write_String(MyLCD, " ");       // 14열 비우기
        } else {
            char db[3];
            sprintf(db, "%2d", den);                // 10~32
            LCD16X2_Write_String(MyLCD, db);        // 13~14에 꽉
        }
        prevSigDen = den;
    }

    // ───── 버튼 처리: 기존 동작 유지 ─────
    // short → TIME SIG 설정 화면
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus = 0;
        notch_ui_button_beep();

        currentUIState = UI_METRONOME_TIME_SIGNATURE_SETTING;
        return;
    }

    // long → 모드 선택
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus      = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }



}



void MetronomeTimeSignatureSetting(void)
{
    // 이전 값 캐시
    static uint8_t  prevNum   = 255;
    static uint8_t  prevDen   = 255;
    static uint8_t  prevBlink = 0xFF;
    static uint16_t prevBPM   = 65535;
    static uint8_t  focus     = 0;   // 0=분자, 1=분모

    // 1회 진입 시
    if (!CurrentDisplayStatus) {

        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "SET TIME SIG.    ");

        LCD16X2_Set_Cursor(MyLCD, 2, 16);
        LCD16X2_Write_String(MyLCD, " ");

        prevNum   = 255;
        prevDen   = 255;
        prevBlink = 0xFF;
        prevBPM   = 65535;
        focus     = 0;   // 들어올 때는 무조건 분자부터

        CurrentDisplayStatus = 1;
    }

    // ── 로터리: 포커스된 쪽만 1씩 INC/DEC, 범위 1~32 ──
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        if (focus == 0) {
            if (TimeSignature < 32) TimeSignature++;
        } else {
            if (TimeSignatureDen < 32) TimeSignatureDen++;
        }

        // 기존 스타일 유지: 돌리면 소리
        {
            uint8_t v = (focus == 0) ? TimeSignature : TimeSignatureDen;
            float f = 150.0f + 100.0f * (float)v;
            notch_ui_rotary_click_freq(f);
        }

        rotaryEvent3 = ROTARY_EVENT_NONE;
    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        if (focus == 0) {
            if (TimeSignature > 1) TimeSignature--;
        } else {
            if (TimeSignatureDen > 1) TimeSignatureDen--;
        }

        {
            uint8_t v = (focus == 0) ? TimeSignature : TimeSignatureDen;
            float f = 150.0f + 100.0f * (float)v;
            notch_ui_rotary_click_freq(f);
        }

        rotaryEvent3 = ROTARY_EVENT_NONE;
    }

    // 공통 프레임은 매 주기 호출 (재생/일시정지 아이콘 갱신)

    // ── BPM(2,3~5)은 이 화면에선 항상 보여주던 패턴 그대로 ──
    if (prevBPM != MetronomeBPM) {
        char buf[4];
        sprintf(buf, "%3d", MetronomeBPM);
        LCD16X2_Set_Cursor(MyLCD, 2, 3);
        LCD16X2_Write_String(MyLCD, buf);
        prevBPM = MetronomeBPM;
    }

    // ── 분자 출력 (2,10~11) ───────────────────────────
    // 포커스가 "아닌" 상태에서는 무조건 보여야 한다.
    {
        uint8_t num = TimeSignature;
        char nb[3];
        sprintf(nb, "%2d", num);   // 4 -> " 4", 12 -> "12"

        LCD16X2_Set_Cursor(MyLCD, 2, 10);
        if (focus == 0) {
            // 지금 이걸 설정 중일 때만 깜빡
            if (UITotalBlinkStatus) {
                LCD16X2_Write_String(MyLCD, "  ");
            } else {
                LCD16X2_Write_String(MyLCD, nb);
            }
        } else {
            // 설정 안 하는 중이면 무조건 표시
            LCD16X2_Write_String(MyLCD, nb);
        }
        prevNum = num;
    }

    // 슬래시는 고정 (2,12)
    LCD16X2_Set_Cursor(MyLCD, 2, 12);
    LCD16X2_Write_String(MyLCD, "/");

    // ── 분모 출력 (2,13~14) ───────────────────────────
    // 요구사항: 9 이하 한 자리면 13열에 붙이고 14열은 공백
    {
        uint8_t den = TimeSignatureDen;

        if (focus == 1) {
            // 지금 분모를 편집 중이면 깜빡
            if (UITotalBlinkStatus) {
                LCD16X2_Set_Cursor(MyLCD, 2, 13);
                LCD16X2_Write_String(MyLCD, "  ");
            } else {
                if (den < 10) {
                    LCD16X2_Set_Cursor(MyLCD, 2, 13);
                    LCD16X2_Write_Char(MyLCD, '0' + den);
                    LCD16X2_Write_String(MyLCD, " ");   // 14열 비우기
                } else {
                    char db[3];
                    sprintf(db, "%2d", den);           // 10~32
                    LCD16X2_Set_Cursor(MyLCD, 2, 13);
                    LCD16X2_Write_String(MyLCD, db);
                }
            }
        } else {
            // 편집 안 하는 동안은 항상 보이게
            if (den < 10) {
                LCD16X2_Set_Cursor(MyLCD, 2, 13);
                LCD16X2_Write_Char(MyLCD, '0' + den);
                LCD16X2_Write_String(MyLCD, " ");
            } else {
                char db[3];
                sprintf(db, "%2d", den);
                LCD16X2_Set_Cursor(MyLCD, 2, 13);
                LCD16X2_Write_String(MyLCD, db);
            }
        }

        prevDen = den;
    }

    prevBlink = UITotalBlinkStatus;

    // ── 버튼 처리 ────────────────────────────────────
    // 짧게 누르면: 분자 → 분모 → TIMING CALC 순으로 넘어감
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        notch_ui_button_beep();

        if (focus == 0) {
            // 분자에서 → 분모로만 이동, 화면은 그대로
            focus = 1;

            // 바로 다시 그리게 하기 위해 캐시 깨기
            prevNum   = 255;
            prevDen   = 255;
            prevBlink = 0xFF;
        } else {
            // 분모까지 끝났으면 원래 코드 흐름대로 BPM 오토계산 화면으로
            CurrentDisplayStatus = 0;
            currentUIState       = UI_METRONOME_TIMING_CALCULATION;
            return;
        }
    }

    // 길게 누르면: 모드 선택 (원래 코드 유지)
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus   = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }

}



/* --- METRONOME TIMING CALCULATION --- */
/* --- METRONOME TIMING CALCULATION --- */
void MetronomeTimingCalc(void)
{


    /* ====== [A] TIM2 HAL 기반 단일 초기화 (1MHz 틱) ====== */
    enum { REQ_TAPS = 16 };
    static TIM_HandleTypeDef htim2;
    static uint8_t  s_tim2_inited   = 0;
    static uint32_t s_tim2_tick_hz  = 1000000U;   // 목표: 1us/틱
    const  uint32_t DEBOUNCE_TICKS  = 50000U;     // 50ms @ 1MHz
    const  uint32_t FLASH_TICKS     = 100000U;     // 색상 플래시 길이(약 30ms)
    static uint32_t flash_until_ts  = 0;          // 색상 복귀 시각(TIM2 기준)

    static uint8_t s_prev_apir_saved = 0;
    static uint8_t s_prev_apir = 0;
    // 첫 진입 시 1회만 저장
    if (!s_prev_apir_saved) {
        s_prev_apir = AudioProcessingIsReady;
        s_prev_apir_saved = 1;
    }
    // 화면에 있는 동안엔 항상 1 유지
    if (AudioProcessingIsReady != 1) {
        AudioProcessingIsReady = 1;
    }

    if (!s_tim2_inited) {
        /* 타이머 클럭 계산: APB1 분주가 1이 아니면 타이머클럭 = 2 * PCLK1 */
        RCC_ClkInitTypeDef clk;
        uint32_t flashLatency;
        HAL_RCC_GetClockConfig(&clk, &flashLatency);

        uint32_t pclk1  = HAL_RCC_GetPCLK1Freq();
        uint32_t timclk = (clk.APB1CLKDivider == RCC_HCLK_DIV1) ? pclk1 : (pclk1 * 2U);

        /* 1MHz 틱을 위한 PSC 계산 (예: timclk=72MHz → PSC=72-1=71) */
        uint32_t target = 1000000U;
        uint32_t psc    = (timclk / target) - 1U;
        if (psc > 0xFFFFU) psc = 0xFFFFU;   // 방어

        __HAL_RCC_TIM2_CLK_ENABLE();

        TIM_ClockConfigTypeDef sClockSourceConfig = {0};
        TIM_MasterConfigTypeDef sMasterConfig     = {0};

        htim2.Instance               = TIM2;
        htim2.Init.Prescaler         = (uint16_t)psc;
        htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
        htim2.Init.Period            = 0xFFFFFFFFU;          // 32-bit full range
        htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
        htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

        if (HAL_TIM_Base_Init(&htim2) != HAL_OK) { Error_Handler(); }

        sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
        if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK) { Error_Handler(); }

        sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
        sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
        if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK) { Error_Handler(); }

        if (HAL_TIM_Base_Start(&htim2) != HAL_OK) { Error_Handler(); }

        s_tim2_tick_hz = target;   // 1us/틱 보장
        s_tim2_inited  = 1;
    }

    /* ====== [B] 화면/상태: 첫 진입 시 한 번만 구성 ====== */
    static uint8_t  tap_count      = 0;     // 0~16
    static uint8_t  intervals_cnt  = 0;     // tap_count-1
    static uint64_t sum_dt_ticks   = 0;
    static uint32_t last_ts        = 0;
    static uint32_t last_edge_ts   = 0;
    static uint16_t prevShownBPM   = 65535;
    static uint8_t  started        = 0;     // 첫 탭 발생 여부

    if (!CurrentDisplayStatus) {

        LCDColorSet(2);

        LCD16X2_ClearCustomChars(0);
        LCD16X2_RegisterCustomChar(0, 0, Playing);
        LCD16X2_RegisterCustomChar(0, 1, Paused);
        LCD16X2_RegisterCustomChar(0, 2, MetronomePriv);
        LCD16X2_RegisterCustomChar(0, 3, MetronomeNow);

        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "BPM AUTO CALC   ");

        HAL_Delay(100);

        // 2행: "Play/Pause to start"
        LCD16X2_DisplayCustomChar(0, 2, 1, 0); // Playing
        LCD16X2_Set_Cursor(MyLCD, 2, 2);
        LCD16X2_Write_String(MyLCD, "/");
        LCD16X2_DisplayCustomChar(0, 2, 3, 1); // Paused
        LCD16X2_Set_Cursor(MyLCD, 2, 5);
        LCD16X2_Write_String(MyLCD, "to start");
        HAL_Delay(100);

        MetronomeHomeFirstRunFlag = 0;

        tap_count      = 0;
        intervals_cnt  = 0;
        sum_dt_ticks   = 0;
        last_ts        = 0;
        last_edge_ts   = 0;
        prevShownBPM   = 65535;
        started        = 0;
        CurrentDisplayStatus = 1;
    }

    /* ====== [C] 공용 메뉴 (2번 버튼 유지) ====== */
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus   = 0;
        MetronomeHomeFirstRunFlag = 0;
        // [NEW] 떠나기 전에 APIR 원복
        if (s_prev_apir_saved) { AudioProcessingIsReady = s_prev_apir; s_prev_apir_saved = 0; }

        notch_ui_button_beep();

        currentUIState = UI_METRONOME_HOME;
        return;
    }
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        // [NEW] 떠나기 전에 APIR 원복
        if (s_prev_apir_saved) { AudioProcessingIsReady = s_prev_apir; s_prev_apir_saved = 0; }


        CurrentDisplayStatus   = 0;
        ModeSelectionFirstRunFlag = 0;


        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }

    /* ====== [D] 0번 버튼: 탭 입력 처리 ====== */
    if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {

    	//notch_ui_button_beep();

        buttonEvents[0] = BUTTON_EVENT_NONE;

        // 완료 후 재시작 요청이면 즉시 리셋
        if (tap_count >= REQ_TAPS) {
        	CurrentDisplayStatus = 0;

            return;
        }

        uint32_t now = __HAL_TIM_GET_COUNTER(&htim2);  // HAL 매크로로 타임스탬프 읽기

        // 첫 탭은 디바운스 패스, 이후 50ms 디바운스
        uint8_t accept = (tap_count == 0) || ((uint32_t)(now - last_edge_ts) >= DEBOUNCE_TICKS);
        if (!accept) goto FLASH_MAINTAIN;  // 바운스면 플래시 상태만 유지/복귀 체크

        last_edge_ts = now;

        // === 색상 플래시: 누르는 순간 5로, 지정시간 뒤 자동 2로 복귀(딜레이 없음) ===
        LCDColorSet(5);
        flash_until_ts = now + FLASH_TICKS;

        // === 탭 카운트/간격 처리 ===
        if (tap_count == 0) {
            // 첫 탭: 진행바 초기화(2행 전체 지우고 아이콘 자리 확보)
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, "                "); // 16칸 비우기
            started  = 1;
            last_ts  = now;
            tap_count = 1;
        } else if (tap_count < REQ_TAPS) {
            uint32_t dt = (uint32_t)(now - last_ts);   // 32-bit wrap 안전
            sum_dt_ticks  += (uint64_t)dt;
            intervals_cnt += 1;
            last_ts        = now;
            tap_count     += 1;
        }

        // === 진행바 그리기 (switch 사용: 마지막 위치만 MetronomeNow, 그 왼쪽은 MetronomePriv) ===
        if (started) {
            uint8_t pos = (tap_count > REQ_TAPS) ? REQ_TAPS : tap_count; // 1..16
            // 먼저 왼쪽 채움(Priv) — 루프 대신 간단히 지우고 switch에서 Now만 찍을 수도 있지만
            // 요구사항에 맞춰 switch로 '마지막 Now 위치'를 선택해 그 외는 Priv로 채운다.
            // 2행 전체를 Priv로 채우고, 아직 누르지 않은 영역은 공백으로 유지하려면 pos 이후는 공백으로 둔다.
            // 여기서는 pos-1까지 Priv, pos는 Now, pos+1..16은 공백.

            // 좌측 Priv들
            for (uint8_t c = 1; c < pos; ++c) {
                LCD16X2_DisplayCustomChar(0, 2, c, 2); // MetronomePriv
            }
            // 우측 공백들
            for (uint8_t c = pos + 1; c <= 16; ++c) {
                LCD16X2_Set_Cursor(MyLCD, 2, c);
                LCD16X2_Write_String(MyLCD, " ");
            }

            // 마지막(가장 오른쪽) 한 개만 Now 아이콘: switch로 위치 선정
            switch (pos) {
                case 1:  LCD16X2_DisplayCustomChar(0, 2, 1, 3);  break;
                case 2:  LCD16X2_DisplayCustomChar(0, 2, 2, 3);  break;
                case 3:  LCD16X2_DisplayCustomChar(0, 2, 3, 3);  break;
                case 4:  LCD16X2_DisplayCustomChar(0, 2, 4, 3);  break;
                case 5:  LCD16X2_DisplayCustomChar(0, 2, 5, 3);  break;
                case 6:  LCD16X2_DisplayCustomChar(0, 2, 6, 3);  break;
                case 7:  LCD16X2_DisplayCustomChar(0, 2, 7, 3);  break;
                case 8:  LCD16X2_DisplayCustomChar(0, 2, 8, 3);  break;
                case 9:  LCD16X2_DisplayCustomChar(0, 2, 9, 3);  break;
                case 10: LCD16X2_DisplayCustomChar(0, 2,10, 3);  break;
                case 11: LCD16X2_DisplayCustomChar(0, 2,11, 3);  break;
                case 12: LCD16X2_DisplayCustomChar(0, 2,12, 3);  break;
                case 13: LCD16X2_DisplayCustomChar(0, 2,13, 3);  break;
                case 14: LCD16X2_DisplayCustomChar(0, 2,14, 3);  break;
                case 15: LCD16X2_DisplayCustomChar(0, 2,15, 3);  break;
                case 16: LCD16X2_DisplayCustomChar(0, 2,16, 3);  break;
                default: break;
            }
        }
    }

FLASH_MAINTAIN:
    /* ====== 색상 플래시 유지/복귀(비차단) ====== */
    if (flash_until_ts != 0U) {
        uint32_t tnow = __HAL_TIM_GET_COUNTER(&htim2);
        // 래핑 안전 비교: (int32_t)(tnow - flash_until_ts) >= 0 → 만료
        if ((int32_t)(tnow - flash_until_ts) >= 0) {
            LCDColorSet(2);
            flash_until_ts = 0U;
        }
    }

    /* ====== [E] 완료 시: 평균 간격 기반 BPM 계산/표시 ====== */
    if (tap_count >= REQ_TAPS && intervals_cnt > 0) {
        uint32_t avg_dt_ticks = (uint32_t)(sum_dt_ticks / intervals_cnt);

        // 1) 탭으로부터 "지금 때리고 있는 그 노트"의 BPM을 먼저 구함
        uint32_t bpm_u32 = ((60UL * s_tim2_tick_hz) + (avg_dt_ticks/2U)) / avg_dt_ticks;

        // 2) 분모를 봐서 "4분음표 기준 BPM"으로 되돌린다
        extern uint8_t TimeSignatureDen;
        uint8_t den = TimeSignatureDen;
        if (den == 0) den = 4;          // 안전빵

        // 4/den 을 곱해야 하니까 이렇게 정수로
        // bpm = bpm_u32 * 4 / den  (반올림)
        bpm_u32 = (bpm_u32 * 4U + (den/2U)) / (uint32_t)den;

        // 3) 기존 클램프
        if (bpm_u32 < 30U)  bpm_u32 = 30U;
        if (bpm_u32 > 300U) bpm_u32 = 300U;

        if ((uint16_t)bpm_u32 != prevShownBPM) {
            MetronomeBPM = (uint16_t)bpm_u32;
            prevShownBPM = (uint16_t)bpm_u32;



            if (MetronomeBPM < 30) {
            	LCD16X2_DisplayCustomChar(0, 1, 4, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 5);
                LCD16X2_Write_String(MyLCD, "                ");
            } else if (MetronomeBPM < 40){
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 3);
                LCD16X2_Write_String(MyLCD, "                ");
            } else if (MetronomeBPM < 60){
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 7);
                LCD16X2_Write_String(MyLCD, "                ");
            } else if (MetronomeBPM < 66){
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 7, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 8);
                LCD16X2_Write_String(MyLCD, "                ");

            } else if (MetronomeBPM < 76) {
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 7, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 8, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 9);
                LCD16X2_Write_String(MyLCD, "                ");

            } else if (MetronomeBPM < 108) {
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 7, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 8, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 9, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 10);
                LCD16X2_Write_String(MyLCD, "                ");

            } else if (MetronomeBPM < 120){
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 7, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 8, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 9, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 10, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 11);
                LCD16X2_Write_String(MyLCD, "                ");

            } else if (MetronomeBPM < 140) {
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 7, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 8, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 9, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 10, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 11, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 12);
                LCD16X2_Write_String(MyLCD, "                ");

            } else if (MetronomeBPM < 168) {
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 7, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 8, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 9, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 10, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 11, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 12, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 13);
                LCD16X2_Write_String(MyLCD, "                ");

            } else if (MetronomeBPM < 302) {
            	LCD16X2_DisplayCustomChar(0, 1, 4, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 5, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 6, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 7, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 8, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 9, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 10, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 11, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 12, 2);
            	LCD16X2_DisplayCustomChar(0, 1, 13, 3);
                LCD16X2_Set_Cursor(MyLCD, 1, 14);
                LCD16X2_Write_String(MyLCD, "                ");

            }

            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            LCD16X2_Write_String(MyLCD, "<S ");
            LCD16X2_Set_Cursor(MyLCD, 1, 14);
            LCD16X2_Write_String(MyLCD, " F>");

            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            LCD16X2_Write_String(MyLCD, "=     BPM        ");
            char buf[5];
            sprintf(buf, "%3u", MetronomeBPM);
            LCD16X2_Set_Cursor(MyLCD, 2, 3);
            LCD16X2_Write_String(MyLCD, buf);

            prevShownBPM = MetronomeBPM;
        }
        // 다시 측정하려면 0번 버튼 한 번 더 → 상단에서 재초기화
    }
}






void SoundGenIntro(void) {
    static uint8_t first = 0;
    if (!first) {
        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "FREQ GEN.");
        LCD16X2_ScrollTextDelay(
            MyLCD,
            "Freqency Generator Mode",
            200, 0, 2, 1
        );
        first = 1;
    }
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }
}

// --- SOUND GENERATOR HOME ---
void SoundGenHome(void) {
    if (!SoundGenHomeFirstRunFlag) {

        LCDColorSet(3);
        LCD16X2_Clear(MyLCD);
        LCD16X2_ClearCustomChars(0);

        LCD16X2_RegisterCustomChar(0, 0, Playing);
        LCD16X2_RegisterCustomChar(0, 1, Paused);
        LCD16X2_RegisterCustomChar(0, 2, SineShape1);
        LCD16X2_RegisterCustomChar(0, 3, SineShape2);
        LCD16X2_RegisterCustomChar(0, 4, SquareShape1);
        LCD16X2_RegisterCustomChar(0, 5, SquareShape2);
        LCD16X2_RegisterCustomChar(0, 6, TriangleShape1);
        LCD16X2_RegisterCustomChar(0, 7, TriangleShape2);



        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "SOUND GEN");

        LCD16X2_Set_Cursor(MyLCD, 1, 12);
        char abuf[6]; sprintf(abuf, "%3d", TunerBaseFreq);
        LCD16X2_Write_String(MyLCD, abuf);
        LCD16X2_Write_String(MyLCD, "Hz");

        LCD16X2_Set_Cursor(MyLCD, 2, 5);
        LCD16X2_Write_String(MyLCD, NoteNames[CurrentNoteIndex]);


        SoundGenHomeFirstRunFlag = 1;
    }

    // 좌측 상태 아이콘
    if (IsSoundGenReady == 1) LCD16X2_DisplayCustomChar(0, 2, 16, 0);
    else                      LCD16X2_DisplayCustomChar(0, 2, 16, 1);

    switch (SoundGenMode){
    case 0:
        LCD16X2_DisplayCustomChar(0, 2, 1, 2);
        LCD16X2_DisplayCustomChar(0, 2, 2, 3);
    break;
    case 1:
        LCD16X2_DisplayCustomChar(0, 2, 1, 4);
        LCD16X2_DisplayCustomChar(0, 2, 2, 5);
    break;
    case 2:
        LCD16X2_DisplayCustomChar(0, 2, 1, 6);
        LCD16X2_DisplayCustomChar(0, 2, 2, 7);
    }



    // 로터리로 음계 조절
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        if (CurrentNoteIndex < (sizeof(NoteNames)/sizeof(NoteNames[0])) - 1) CurrentNoteIndex++;
        notch_ui_rotary_click_freq( 1200.f );

        rotaryEvent3 = ROTARY_EVENT_NONE;
    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        if (CurrentNoteIndex > 0) CurrentNoteIndex--;
        notch_ui_rotary_click_freq( 600.f );

        rotaryEvent3 = ROTARY_EVENT_NONE;
    }



    // ---- 바뀔 때만 갱신 (순서 중요!) ----
    static uint8_t  prev_note = 0xFF;
    static uint16_t prev_Aref = 0xFFFF;

    uint8_t note_changed = (prev_note != CurrentNoteIndex);
    uint8_t aref_changed = (prev_Aref != TunerBaseFreq);

    // 1) 화면 표시는 note 바뀔 때만
    if (note_changed) {
        LCD16X2_Set_Cursor(MyLCD, 2, 5);
        LCD16X2_Write_String(MyLCD, "     "); // 잔상 지우기
        LCD16X2_Set_Cursor(MyLCD, 2, 5);
        LCD16X2_Write_String(MyLCD, NoteNames[CurrentNoteIndex]);
    }

    // 2) 주파수 값 갱신은 note/aref 둘 중 하나라도 바뀌었을 때
    if (note_changed || aref_changed) {
        float newFreq = Tuner_NoteToFreq(CurrentNoteIndex, 0 /*cent*/, TunerBaseFreq);
        // 너무 민감하지 않게 소폭 변화는 무시
        if (fabsf(newFreq - SoundFrequencyOutput) > 0.05f) {
            SoundFrequencyOutput = newFreq;
        }
        // 여기서 '나중에' prev들을 갱신해야 위의 조건이 제대로 동작
        prev_note = CurrentNoteIndex;
        prev_Aref = TunerBaseFreq;
    }

    // SHORT → A= 설정
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        SoundGenHomeFirstRunFlag = 0;
        notch_ui_button_beep();

        currentUIState = UI_SOUNDGEN_BASE_A_FREQ_SETTING;
        return;
    }

    // Play/Pause
    if (buttonEvents[0] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[0] = BUTTON_EVENT_NONE;

        notch_ui_button_beep();

        IsSoundGenReady ^= 1;
        return;
    }

    // PlayPauseLongPress
    if (buttonEvents[0] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[0] = BUTTON_EVENT_NONE;

        if (SoundGenMode == 2) {
        	SoundGenMode = 0;
        } else  {
        	SoundGenMode++;
        }
        notch_ui_button_beep();
        return;
    }

    // LONG → 메뉴
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        SoundGenHomeFirstRunFlag = 0;
        ModeSelectionFirstRunFlag = 0;


        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }
}



void SoundGenBaseAFreqSetting(void) {
    static uint16_t prevFreq = 65535;
    static uint8_t  prevBlink = 0xFF;   // ✨ 깜빡이 상태 캐시 추가

    if (!CurrentDisplayStatus) {

        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "A4 REF");
        LCD16X2_Set_Cursor(MyLCD, 2, 11);
        LCD16X2_Write_String(MyLCD, "           ");

        LCD16X2_Set_Cursor(MyLCD, 1, 15);
        LCD16X2_Write_String(MyLCD, "Hz");
        CurrentDisplayStatus = 1;

        prevFreq  = 65535;   // 강제 출력 유도
        prevBlink = 0xFF;    // ✨ 깜빡이 강제 반영
    }

    // 🌀 rotary 조절
    if (rotaryEvent3 == ROTARY_EVENT_CW) {
        if (TunerBaseFreq < 480) {  // 예: 최대 500Hz 제한
            TunerBaseFreq += 1;
        }

        notch_ui_rotary_click_freq( 1200.f );

        rotaryEvent3 = ROTARY_EVENT_NONE;
    } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
        if (TunerBaseFreq > 400) {  // 예: 최소 300Hz 제한
            TunerBaseFreq -= 1;
        }

        notch_ui_rotary_click_freq( 800.f );

        rotaryEvent3 = ROTARY_EVENT_NONE;
    }

    // 🖥️ 값 출력 (✨ 숫자만 깜빡이)
    if (TunerBaseFreq != prevFreq || prevBlink != UITotalBlinkStatus) {
        LCD16X2_Set_Cursor(MyLCD, 1, 12);  // A= 뒤에 출력 (숫자 위치 고정)
        if (UITotalBlinkStatus) {
            LCD16X2_Write_String(MyLCD, "   ");  // ✨ 깜빡일 때 숫자 자리만 공백(3칸)
        } else {
            char buf[6];
            sprintf(buf, "%3d", TunerBaseFreq);
            LCD16X2_Write_String(MyLCD, buf);
        }
        prevFreq  = TunerBaseFreq;
        prevBlink = UITotalBlinkStatus;
    }

    // 🚪 long → 메뉴로 복귀
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_mode_return_triple_beep();

        currentUIState = UI_MODE_SELECTION;
        return;
    }

    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        CurrentDisplayStatus = 0;
        ModeSelectionFirstRunFlag = 0;

        notch_ui_button_beep();

        currentUIState = UI_SOUNDGEN_HOME;
        return;
    }


}







/* --- SETTINGS HOME --- */
// === REPLACEMENT: SettingsHome (drop-in) =====================================
/* --- SETTINGS HOME (drop-in replacement) ---------------------------------- */
// SettingsHome() 안에 drop-in 패치
// - enum/라벨에 RESET 항목 추가
// - ENTER: 1회 → SURE? , 2회 → SAVE→RESET
// - 다른 메뉴로 이동하면 SURE? 취소
void SettingsHome(void) {


    // 기존 전역/유틸은 프로젝트에 이미 존재
    // AutoVU_After10s, MicAGC_On, MicBoost_dB, MicInputMode, SFXVolume,
    // notch_ui_rotary_click_freq(), notch_ui_button_beep(), LCD APIs,
    // SettingsHomeFirstRunFlag, CurrentModeStatus, UITotalBlinkStatus,
    // buttonEvents[], rotaryEvent3, currentUIState 등
    enum {
        ST_AUTO_VU = 0,
        ST_MIC_AGC,
        ST_MIC_GAIN,
        ST_MIC_MODE,
        ST_SFX_VOL,
        ST_TEMP_UNIT,
        ST_TEMP_CAL,
        ST_TNR_CAL,
        ST_CLOCK_SET,      // 시간 설정
        ST_MET_RIT_LEN,    // ★ NEW: RIT/ACCEL 길이(박 수)
        ST_MET_RIT_CURVE,  // ★ NEW: RIT/ACCEL 곡선 프리셋
        ST_ABOUT,
        ST_RESET,
        ST_BACK,
        ST_COUNT
    };

    static const char* kMenuLabel[ST_COUNT] = {
        "AUTO VU   ",
        "MIC AGC   ",
        "MIC GAIN  ",
        "MIC MODE  ",
        "SFX VOL   ",
        "TEMP UNIT ",
        "TEMP CAL  ",
        "TUNER CAL ",
        "CLOCK SET?",
        "RIT LEN   ",   // ★ 몇 박에 걸쳐 rit/accel
        "RIT CURV  ",   // ★ NORMAL / SOFT / FAST
        "ABOUT DEV ",
        "RESET ?   ",
        "BACK MENU ",
    };


    static const char*    kMicModeName[3] = { "STEREO", " L-EXP", " R-EXP" };

    // 편집 모드: 여러 항목 지원(기존엔 MIC GAIN 전용이었음)
    static uint8_t inEdit = 0;

    // RESET 확인 단계
    static uint8_t confirmReset = 0;

    // 화면 캐시(플리커 최소화)
    static int     prevMenu   = -1;
    static char    prevVal[7] = "";   // 값 영역 6칸 + NUL
    static uint8_t prevBlink  = 0xFF; // 깜빡임 추적

    // ── 첫 진입 초기화 ──
    if (!SettingsHomeFirstRunFlag) {
        LCDColorSet(4);
        LCD16X2_Clear(MyLCD);
        LCD16X2_ClearCustomChars(0);

        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "SETTINGS");

        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, "          "); // 라벨 10칸
        LCD16X2_Set_Cursor(MyLCD, 2, 11);
        LCD16X2_Write_String(MyLCD, "      ");     // 값 6칸

        inEdit = 0;
        confirmReset = 0;
        prevMenu  = -1;
        prevVal[0] = '\0';
        prevBlink  = 0xFF;

        SettingsHomeFirstRunFlag = 1;
    }

    // ── 로터리 ──
    if (!inEdit) {
        if (rotaryEvent3 == ROTARY_EVENT_CW) {
            if (++CurrentModeStatus >= ST_COUNT) CurrentModeStatus = 0;
            notch_ui_rotary_click_freq(1200.f);
            rotaryEvent3 = ROTARY_EVENT_NONE;
        } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
            if (CurrentModeStatus == 0) CurrentModeStatus = ST_COUNT - 1;
            else CurrentModeStatus--;
            notch_ui_rotary_click_freq(800.f);
            rotaryEvent3 = ROTARY_EVENT_NONE;
        }
    } else {
        // 편집 모드: 항목별로 로터리 동작
        switch (CurrentModeStatus) {
            case ST_MIC_GAIN: { // ±3 dB step, -12..+12
                if (rotaryEvent3 == ROTARY_EVENT_CW)   { if (MicBoost_dB <= 9)  MicBoost_dB += 3; notch_ui_rotary_click_freq(1100.f); }
                if (rotaryEvent3 == ROTARY_EVENT_CCW)  { if (MicBoost_dB >= -9) MicBoost_dB -= 3; notch_ui_rotary_click_freq(900.f);  }
                rotaryEvent3 = ROTARY_EVENT_NONE;
            } break;

            case ST_SFX_VOL: { // 0..50
                if (rotaryEvent3 == ROTARY_EVENT_CW)   { if (SFXVolume < 50) SFXVolume++; notch_ui_rotary_click_freq(1100.f); }
                if (rotaryEvent3 == ROTARY_EVENT_CCW)  { if (SFXVolume > 0)  SFXVolume--; notch_ui_rotary_click_freq(900.f);  }
                rotaryEvent3 = ROTARY_EVENT_NONE;
            } break;

            case ST_TEMP_CAL: { // -9.9 .. +9.9, 0.1 step
                int tenth = (int)(Cal_TempC_Offset * 10.0f + (Cal_TempC_Offset>=0?0.5f:-0.5f));
                if (rotaryEvent3 == ROTARY_EVENT_CW)   { if (tenth < 99)  tenth++; notch_ui_rotary_click_freq(1100.f); }
                if (rotaryEvent3 == ROTARY_EVENT_CCW)  { if (tenth > -99) tenth--; notch_ui_rotary_click_freq(900.f);  }
                Cal_TempC_Offset = (float)tenth / 10.0f;
                rotaryEvent3 = ROTARY_EVENT_NONE;
            } break;

            case ST_TNR_CAL: { // 400..480 Hz, 1 step
                uint32_t step = kTnrSteps[tnrStepIdx];
                if (rotaryEvent3 == ROTARY_EVENT_CW) {
                    if (TunerCalibrationValue <= (UINT32_MAX - step)) TunerCalibrationValue += step;
                    if (TunerCalibrationValue > 30000u) TunerCalibrationValue = 30000u; // 상한 가드(임의)
                    notch_ui_rotary_click_freq(1200.f);
                    rotaryEvent3 = ROTARY_EVENT_NONE;
                } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
                    if (TunerCalibrationValue >= step) TunerCalibrationValue -= step;
                    else TunerCalibrationValue = 0u;
                    notch_ui_rotary_click_freq(800.f);
                    rotaryEvent3 = ROTARY_EVENT_NONE;
                }
                // ★ 값 변경 즉시 튜너 보정 반영
                notch_tuner_set_fs_trim_ppm(TunerCalibrationValue);

            } break;

            /* [INSERT CASE in "편집 중 로터리 처리" switch] */
            case ST_MET_RIT_LEN: {
                if (rotaryEvent3 == ROTARY_EVENT_CW) {
                    if (g_met_rit_accel_beats < 16) g_met_rit_accel_beats++;
                    notch_ui_rotary_click_freq(1200.f);
                    rotaryEvent3 = ROTARY_EVENT_NONE;
                } else if (rotaryEvent3 == ROTARY_EVENT_CCW) {
                    if (g_met_rit_accel_beats > 1) g_met_rit_accel_beats--;
                    notch_ui_rotary_click_freq(800.f);
                    rotaryEvent3 = ROTARY_EVENT_NONE;
                }
            } break;

            case ST_MET_RIT_CURVE: {
                // 로터리 이벤트는 소비만 하고 값 변경은 하지 않음
                if (rotaryEvent3 == ROTARY_EVENT_CW || rotaryEvent3 == ROTARY_EVENT_CCW) {
                    rotaryEvent3 = ROTARY_EVENT_NONE;
                }
            } break;

            default:
                // 편집 모드가 아닌 항목이면 자동 해제
                inEdit = 0;
                break;
        }
    }

    // ── 버튼(짧게) ──
    // ── 버튼(짧게) ──
    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        notch_ui_button_beep();

        switch (CurrentModeStatus) {
            case ST_AUTO_VU:  AutoVU_After10s ^= 1u; break;
            case ST_MIC_AGC:  MicAGC_On       ^= 1u; break;

            // ★ 편집 항목: SHORT = 편집 토글(on/off)로만 동작 (스텝 순환 제거)
            case ST_MIC_GAIN:
            case ST_SFX_VOL:
            case ST_TEMP_CAL:
            case ST_TNR_CAL: {
                if (!inEdit) {
                    inEdit    = 1;     // 편집 진입 → 깜빡임 시작
                    prevBlink = 0xFF;  // 깜빡임 에지 리셋
                } else {
                    inEdit    = 0;     // 편집 종료 → 깜빡임 정지, 메뉴 이동 모드 복귀
                    prevBlink = 0xFF;
                    prevVal[0] = '\0'; // 값 재그리기 유도(잔상 방지)
                }
            } break;

            case ST_MIC_MODE:
                MicInputMode = (uint8_t)((MicInputMode + 1u) % 3u);
                break;

            case ST_TEMP_UNIT:
                TempUnitF ^= 1u; // 0↔1
                break;

            case ST_ABOUT:
                inEdit = 0;
                rotaryEvent3 = ROTARY_EVENT_NONE;
                buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
                SettingsHomeFirstRunFlag = 0;
                currentUIState = UI_SETTINGS_ABOUT;
                return;

                // ★ 우리가 추가하는 부분
                case ST_CLOCK_SET: {
                    // 입력 찌꺼기 비우기
                    rotaryEvent3 = ROTARY_EVENT_NONE;
                    buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;

                    // 시간 설정 UI 들어가기
                    Clock_UI_SetTime();

                    // 돌아오면 설정화면 다시 그리게 플래그 리셋
                    SettingsHomeFirstRunFlag = 0;
                    return;
                }

            case ST_RESET: {
                if (!confirmReset) {
                    confirmReset = 1;
                    prevVal[0] = '\0';  // 값 재그리기 유도
                } else {
                    extern void ConfigStorage_Service(uint8_t trigger_save);
                    LCDColorSet(4);
                    LCD16X2_Set_Cursor(MyLCD, 2, 11);
                    LCD16X2_Write_String(MyLCD, "SAVING");
                    ConfigStorage_Service(1);
                    HAL_Delay(150);

                    LCDColorSet(5);
                    LCD16X2_Set_Cursor(MyLCD, 2, 11);
                    LCD16X2_Write_String(MyLCD, "RESET ");
                    HAL_Delay(150);

                    NVIC_SystemReset();
                }
            } break;

            case ST_BACK:
                inEdit = 0;
                rotaryEvent3 = ROTARY_EVENT_NONE;
                buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
                SettingsHomeFirstRunFlag = 0;
                currentUIState = UI_MODE_SELECTION;
                return;

            case ST_MET_RIT_LEN: {       // [NEW]
                if (!inEdit) {
                    inEdit    = 1;
                    prevBlink = 0xFF;
                } else {
                    inEdit    = 0;
                    prevBlink = 0xFF;
                    prevVal[0]= '\0';
                }
            } break;

            case ST_MET_RIT_CURVE: {
                uint8_t p = g_met_rit_curve_preset;
                if (p >= MET_RIT_PRESET_COUNT) p = MET_RIT_PRESET_NORMAL;
                p = (uint8_t)((p + 1u) % MET_RIT_PRESET_COUNT);   // NORMAL → SOFT → FAST → ...
                g_met_rit_curve_preset = p;

                // 편집 토글은 사용하지 않음(로터리 편집 제거에 맞춰 일관성 유지)
                inEdit    = 0;
                prevBlink = 0xFF;     // 깜빡임 에지 리셋
                prevVal[0]= '\0';     // 재그리기 유도
            } break;

        }
    }


    // ── 버튼(길게): 메인 메뉴 복귀 ──
    // ── 버튼(길게): 편집 중이면 TNR 스텝 순환, 아니면 메인 메뉴 복귀 ──
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;

        if (inEdit && CurrentModeStatus == ST_TNR_CAL) {
            // ★ 길게: 스텝 순환 (1→10→50→100→500→1000)
            tnrStepIdx = (uint8_t)((tnrStepIdx + 1u) % (uint8_t)(sizeof(kTnrSteps)/sizeof(kTnrSteps[0])));

            // 피드백 톤(스텝 커질수록 저음)
            float f = 1500.f - 120.f * (float)tnrStepIdx;
            if (f < 600.f) f = 600.f;
            notch_ui_rotary_click_freq(f);

            // 편집 모드 유지(깜빡임 계속), 값 재그리기만 유도하려면 아래 한 줄 사용
            // prevVal[0] = '\0';

            return;
        }

        // 편집 중 TNR가 아니면: 기존처럼 메인 메뉴 복귀
        inEdit = 0;
        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0]=buttonEvents[1]=buttonEvents[2]=BUTTON_EVENT_NONE;
        SettingsHomeFirstRunFlag = 0;
        currentUIState = UI_MODE_SELECTION;
        return;
    }


    // ── 라벨: 메뉴 바뀔 때만 갱신 ──
    if (prevMenu != CurrentModeStatus) {
        char lab[11];
        snprintf(lab, sizeof(lab), "%-10.10s", kMenuLabel[CurrentModeStatus]); // 10칸
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, lab);
        prevMenu = CurrentModeStatus;
        prevVal[0] = '\0';
        confirmReset = 0;   // 다른 메뉴로 이동하면 SURE? 취소
        // 편집 메뉴가 아닌 항목으로 이동하면 편집 종료
        if (!(CurrentModeStatus==ST_MIC_GAIN || CurrentModeStatus==ST_SFX_VOL || CurrentModeStatus==ST_MET_RIT_LEN  ||
              CurrentModeStatus==ST_TEMP_CAL || CurrentModeStatus==ST_TNR_CAL || CurrentModeStatus==ST_MET_RIT_CURVE)) {
            inEdit = 0;
        }
    }

    // ── 값(6칸) 구성 ──
    char now[7]; now[0] = '\0';
    switch (CurrentModeStatus) {
        case ST_AUTO_VU:
            snprintf(now, sizeof(now), "%-6.6s", AutoVU_After10s ? "ON" : "OFF");
            break;

        case ST_MIC_AGC:
            snprintf(now, sizeof(now), "%-6.6s", MicAGC_On ? "ON" : "OFF");
            break;

        case ST_MIC_GAIN:
            snprintf(now, sizeof(now), "%+3ddB ", (int)MicBoost_dB); // "+12dB "
            break;

        case ST_MIC_MODE: {
            uint8_t m = (MicInputMode <= 2) ? MicInputMode : 0;
            snprintf(now, sizeof(now), "%-6.6s", kMicModeName[m]);
        } break;

        case ST_SFX_VOL: {
            // 0..50 → " xx/50" (6칸)
            uint8_t v = (SFXVolume <= 50) ? (uint8_t)SFXVolume : 50;
            snprintf(now, sizeof(now), "%3u/50", v);
        } break;

        case ST_TEMP_UNIT:
            snprintf(now, sizeof(now), "%-6.6s", TempUnitF ? "  F   " : "  C   ");
            break;

        case ST_TEMP_CAL: {
            // "-1.2C " 형태(최대 6칸)
            float val = Cal_TempC_Offset;
            if (val >  9.9f) val =  9.9f;
            if (val < -9.9f) val = -9.9f;
            int sgn = (val < 0.0f) ? -1 : 1;
            int t10 = (int)(sgn*val*10.0f + 0.5f);
            int ti  = t10 / 10, tf = t10 % 10;
            snprintf(now, sizeof(now), "%c%1d.%1dC ",
                     (sgn<0?'-':'+'), ti, tf);
        } break;

        case ST_TNR_CAL: {
            // 우측 정렬 6칸 "xxxxx "
            char buf[7];
            snprintf(buf, sizeof(buf), "%6lu", (unsigned long)TunerCalibrationValue);
            memcpy(now, buf, 6);
        } break;

        case ST_ABOUT:
            snprintf(now, sizeof(now), "    =>");
            break;

        case ST_RESET:
            snprintf(now, sizeof(now), "%-6.6s", confirmReset ? "SURE?" : " ");
            break;

        case ST_BACK:
            snprintf(now, sizeof(now), "    =>");
            break;

        case ST_CLOCK_SET:
            snprintf(now, sizeof(now), "    =>");
            break;

        case ST_MET_RIT_LEN: {
            uint8_t b = (g_met_rit_accel_beats >= 1 && g_met_rit_accel_beats <= 99)
                        ? g_met_rit_accel_beats : 4;
            /* 우측 정렬 6칸: "  4BTS" */
            snprintf(now, sizeof(now), "%3uBTS", (unsigned)b);
        } break;

        case ST_MET_RIT_CURVE: {
            uint8_t p = g_met_rit_curve_preset;
            if (p >= MET_RIT_PRESET_COUNT) p = MET_RIT_PRESET_NORMAL;
            snprintf(now, sizeof(now), "%-6.6s", kMetRitCurveName[p]);  // "NORMAL", "SOFT", "FAST"
        } break;

        default:
            snprintf(now, sizeof(now), "      ");
            break;
    }

    // ── 값 쓰기: 편집 중이면 1초 깜빡임 ──
    uint8_t blink = UITotalBlinkStatus;
    int needUpdate = 0;

    if (inEdit &&
        (CurrentModeStatus==ST_MIC_GAIN || CurrentModeStatus==ST_SFX_VOL ||
         CurrentModeStatus==ST_TEMP_CAL || CurrentModeStatus==ST_TNR_CAL || CurrentModeStatus==ST_MET_RIT_LEN)) {
        if (prevBlink != blink) needUpdate = 1;
        prevBlink = blink;
        if (blink) {
            LCD16X2_Set_Cursor(MyLCD, 2, 11);
            LCD16X2_Write_String(MyLCD, "      ");
            strcpy(prevVal, "      ");
            return;
        }
    } else {
        prevBlink = 0xFF;
    }

    if (strcmp(prevVal, now) != 0 || needUpdate) {
        LCD16X2_Set_Cursor(MyLCD, 2, 11);
        LCD16X2_Write_String(MyLCD, now);
        strcpy(prevVal, now);
    }
}






void SettingsHelp(void) { // HELP 아닙니다. 이름만 이렇게 해둠. 헬프가 아니라 메트로놈 시간 설정임.

	// TODO : 아직 구현 안햇음.


}

void SettingsPowerOffConfirm(void) { /// 파워 오프 컨펌 아닙니다. DHT11 작동 테스트이며 지금은 PLACEHOLDER 입니다
    LCDColorSet(4);
    LCD16X2_Clear(MyLCD);

    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "TURN OFF? ");

    LCD16X2_Set_Cursor(MyLCD, 2, 1);
    LCD16X2_Write_String(MyLCD, "Press: CONFIRM");

    if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        // 👉 여기에 실제 전원 OFF 동작 추가 (예: __disable_irq(); NVIC_SystemReset(); 등)
        HAL_SuspendTick();  // SysTick 멈춤 (선택사항)
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);

    }

    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        ModeSelectionFirstRunFlag = 0;
        currentUIState = UI_MODE_SELECTION;
        return;


    }
}


void SettingsAbout(void) {
    LCDColorSet(4);
    LCD16X2_Clear(MyLCD);

    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "TEST VER 0.90");


    LCD16X2_ScrollTextDelay(MyLCD,
        "20250808 MADE BY SHIN SENUG HYUN - VISIT SERIALSNIFFYHECKER ON GITHUB!",
        200, 0, 2, 1);


    // ⬅️ 되돌아가기
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        ModeSelectionFirstRunFlag = 0;
        currentUIState = UI_MODE_SELECTION;
        return;
    }
}



void SettingsFWUpdate(void) {
    LCDColorSet(4);
    LCD16X2_Clear(MyLCD);

    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "FW UPDATE MODE");

    LCD16X2_Set_Cursor(MyLCD, 2, 1);
    LCD16X2_Write_String(MyLCD, "Don't turn off!");

    // 긴 눌림으로 되돌아감
    if (buttonEvents[2] == BUTTON_EVENT_LONG_PRESS) {
        buttonEvents[2] = BUTTON_EVENT_NONE;
        ModeSelectionFirstRunFlag = 0;
        currentUIState = UI_MODE_SELECTION;
        return;
    }

    // 🔧 실제 펌업 모드 진입 로직은 여기에!
}

static void _RegisterVolGlyph(uint8_t slot, const uint8_t *p7) {
    uint8_t tmp[8];
    for (int i = 0; i < 7; ++i) tmp[i] = p7[i];
    tmp[7] = 0x00; // 마지막 라인 패딩
    LCD16X2_RegisterCustomChar(0, slot, tmp);
}

static void VolumeIcons_Register(void) {
    LCD16X2_ClearCustomChars(0);
    _RegisterVolGlyph(0, VOLUMEONE);
    _RegisterVolGlyph(1, VOLUMETWO);
    _RegisterVolGlyph(2, VOLUMETHREE);
    _RegisterVolGlyph(3, VOLUMEFOUR);
    _RegisterVolGlyph(4, VOLUMEFIVE);
    _RegisterVolGlyph(5, VOLUMESIX);
    _RegisterVolGlyph(6, VOLUMESEVEN);
}

static void VolumeIcons_Draw(uint16_t value) {
    uint8_t level = (value == 0) ? 0 : (uint8_t)((value + 7) / 8); // 0..7
    if (level > 7) level = 7;

    // 7칸 영역을 모두 갱신(필요 최소만)
    for (uint8_t i = 0; i < 7; ++i) {
        uint8_t col = 5 + i; // 2,5칸부터
        if (i < level) {
            // i번째 칸엔 i번째 모양을 누적 표시
            LCD16X2_DisplayCustomChar(0, 2, col, i); // (컨트롤러 0, 행2, 열col, 글리프 i)
        } else {
            LCD16X2_Set_Cursor(MyLCD, 2, col);
            LCD16X2_Write_String(MyLCD, " ");
        }
    }
}



void VolumeControl(void) {


    static int8_t prevVolume = -1;

    // 📌 진입 시 1회만 LCD 텍스트 표시
    if (!VolumeControlFirstRunFlag) {

    	LCDColorSet(0);

    	ModeSelectionFirstRunFlag      = 0;
    	PracticeHomeFirstRunFlag       = 0;
    	TunerHomeFirstRunFlag          = 0;
    	MetronomeHomeFirstRunFlag      = 0;
    	SoundGenHomeFirstRunFlag       = 0;
    	SettingsHomeFirstRunFlag       = 0;
    	VolumeControlFirstRunFlag      = 0;
    	BalanceControlFirstRunFlag     = 0;
    	CurrentDisplayStatus           = 0;

        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "MASTER VOLUME");
        LCD16X2_Set_Cursor(MyLCD, 2, 4);
        LCD16X2_Write_String(MyLCD, "[");
        LCD16X2_Set_Cursor(MyLCD, 2, 12);
        LCD16X2_Write_String(MyLCD, "]");

        VolumeIcons_Register();
        VolumeIcons_Draw(MasterVolume);

        VolumeControlFirstRunFlag = 1;
        prevVolume = -1;  // 강제 초기 출력 유도
    }

    // 🎛️ 로터리로 볼륨 조절
    if (rotaryEvent2 == ROTARY_EVENT_CW && MasterVolume < 50) {
        MasterVolume++;
        rotaryEvent2 = ROTARY_EVENT_NONE;
        LastVolumeInteractionTick = VolumeTimer;

        float f = 400.0f + 40.0f * (float)MasterVolume;  // 0~50 → 400~2400 Hz
        notch_ui_rotary_click_freq(f);


    } else if (rotaryEvent2 == ROTARY_EVENT_CCW && MasterVolume > 0) {
        MasterVolume--;
        rotaryEvent2 = ROTARY_EVENT_NONE;
        LastVolumeInteractionTick = VolumeTimer;

        float f = 400.0f + 40.0f * (float)MasterVolume;  // 0~50 → 400~2400 Hz
        notch_ui_rotary_click_freq(f);

    }

    // 📟 값이 바뀌었을 때만 숫자 영역 업데이트
    if (prevVolume != MasterVolume) {
        char buf[17];
        sprintf(buf, "%2d", MasterVolume);
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, buf);

        VolumeIcons_Draw(MasterVolume);

        prevVolume = MasterVolume;

    }

    // 🔘 버튼 1 → 밸런스 모드 진입
    if (buttonEvents[1] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[1] = BUTTON_EVENT_NONE;
        currentUIState = UI_SOUND_BALANCE;
        LastBalanceInteractionTick = VolumeTimer;

        // 상태 전환 초기화
        VolumeControlFirstRunFlag = 0;
        return;
    }

    // ⏳ 타임아웃 또는 메뉴 조작 → 원래 화면 복귀
    if ((VolumeTimer - LastVolumeInteractionTick) >= 5000 ||
        rotaryEvent3 != ROTARY_EVENT_NONE ||
        buttonEvents[0] != BUTTON_EVENT_NONE ||
        buttonEvents[2] != BUTTON_EVENT_NONE) {

        // 소거
        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0] = BUTTON_EVENT_NONE;
        buttonEvents[2] = BUTTON_EVENT_NONE;

        currentUIState = PrevUIBeforeVolume;
        VolumeControlFirstRunFlag = 0;

        // ★ VU가 직전 화면이었다면 즉시 복구: R/L 미니 즉시 표시 + 다음 프레임부터 바로 동작
        return;
    }
}


void BalanceControl(void) {
    static int8_t prevBalance = -1;

    // 📌 최초 진입 시 UI 텍스트만 1회 출력
    if (!BalanceControlFirstRunFlag) {

    	ModeSelectionFirstRunFlag      = 0;
    	PracticeHomeFirstRunFlag       = 0;
    	TunerHomeFirstRunFlag          = 0;
    	MetronomeHomeFirstRunFlag      = 0;
    	SoundGenHomeFirstRunFlag       = 0;
    	SettingsHomeFirstRunFlag       = 0;
    	VolumeControlFirstRunFlag      = 0;
    	BalanceControlFirstRunFlag     = 0;
    	CurrentDisplayStatus           = 0;

    	LCDColorSet(0);
        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "MIC VOLUME");
        LCD16X2_Set_Cursor(MyLCD, 2, 4);
        LCD16X2_Write_String(MyLCD, "[");
        LCD16X2_Set_Cursor(MyLCD, 2, 12);
        LCD16X2_Write_String(MyLCD, "]");;

        VolumeIcons_Register();
        VolumeIcons_Draw(MasterVolume);

        BalanceControlFirstRunFlag = 1;
        prevBalance = -1;  // 강제 출력 유도
    }

    // 🎛️ 로터리로 밸런스 조절
    if (rotaryEvent2 == ROTARY_EVENT_CW && SoundBalance < 50) {
        SoundBalance++;
        rotaryEvent2 = ROTARY_EVENT_NONE;
        LastBalanceInteractionTick = VolumeTimer;

        float f = 400.0f + 40.0f * (float)SoundBalance;  // 0~50 → 400~2400 Hz
        notch_ui_rotary_click_freq(f);

    } else if (rotaryEvent2 == ROTARY_EVENT_CCW && SoundBalance > 0) {
        SoundBalance--;
        rotaryEvent2 = ROTARY_EVENT_NONE;
        LastBalanceInteractionTick = VolumeTimer;


        float f = 400.0f + 40.0f * (float)SoundBalance;  // 0~50 → 400~2400 Hz
        notch_ui_rotary_click_freq(f);


    }

    // 📟 값이 바뀌었을 때만 숫자 출력
    if (prevBalance != SoundBalance) {
        char buf[17];
        sprintf(buf, "%2d", SoundBalance);
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, buf);

        VolumeIcons_Draw(SoundBalance);


        prevBalance = SoundBalance;
    }

    // 🔘 버튼 1 → 원래 화면 복귀
    if (buttonEvents[1] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[1] = BUTTON_EVENT_NONE;
        currentUIState = PrevUIBeforeVolume;
        BalanceControlFirstRunFlag = 0;
        return;
    }

    // ⏳ 타임아웃 또는 메뉴 조작 → 원래 화면 복귀
    // [타임아웃/메뉴 조작] 5초 무입력 또는 다른 조작 → 원래 화면 복귀
    if ((VolumeTimer - LastBalanceInteractionTick) >= 5000 ||
        rotaryEvent3 != ROTARY_EVENT_NONE ||
        buttonEvents[0] != BUTTON_EVENT_NONE ||
        buttonEvents[2] != BUTTON_EVENT_NONE) {

        // 소거
        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0] = BUTTON_EVENT_NONE;
        buttonEvents[2] = BUTTON_EVENT_NONE;

        currentUIState = PrevUIBeforeVolume;
        BalanceControlFirstRunFlag = 0;

        return;
    }

}


// [ADD] 메트로놈 볼륨 UI
static uint8_t MetronomeVolumeFirstRunFlag = 0;

void MetronomeVolumeControl(void)
{
    static int8_t prevMetVol = -1;

    // 최초 진입시 1회 UI 그리기
    if (!MetronomeVolumeFirstRunFlag) {

        LCDColorSet(0);

        // 모든 1회성 플래그 정리(다른 화면과 동일하게 초기화 관례 유지)
        ModeSelectionFirstRunFlag      = 0;
        PracticeHomeFirstRunFlag       = 0;
        TunerHomeFirstRunFlag          = 0;
        MetronomeHomeFirstRunFlag      = 0;
        SoundGenHomeFirstRunFlag       = 0;
        SettingsHomeFirstRunFlag       = 0;
        VolumeControlFirstRunFlag      = 0;
        BalanceControlFirstRunFlag     = 0;
        CurrentDisplayStatus           = 0;

        LCD16X2_Clear(MyLCD);
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        // 16칸 딱 맞음: "METRONOME VOLUME"
        LCD16X2_Write_String(MyLCD, "METRONOME VOLUME");
        LCD16X2_Set_Cursor(MyLCD, 2, 4);
        LCD16X2_Write_String(MyLCD, "[");
        LCD16X2_Set_Cursor(MyLCD, 2, 12);
        LCD16X2_Write_String(MyLCD, "]");

        VolumeIcons_Register();
        VolumeIcons_Draw(MetronomeVolume);

        MetronomeVolumeFirstRunFlag = 1;
        prevMetVol = -1;
    }

    // 로터리로 0..50 조정 (rotaryEvent2는 기존 볼륨/밸런스가 쓰던 채널 그대로)
    if (rotaryEvent2 == ROTARY_EVENT_CW && MetronomeVolume < 50) {
        MetronomeVolume++;
        rotaryEvent2 = ROTARY_EVENT_NONE;
        LastVolumeInteractionTick = VolumeTimer;

        float f = 400.0f + 40.0f * (float)MetronomeVolume;  // 0~50 → 400~2400 Hz
        notch_ui_rotary_click_freq(f);

    } else if (rotaryEvent2 == ROTARY_EVENT_CCW && MetronomeVolume > 0) {
        MetronomeVolume--;
        rotaryEvent2 = ROTARY_EVENT_NONE;
        LastVolumeInteractionTick = VolumeTimer;

        float f = 400.0f + 40.0f * (float)MetronomeVolume;
        notch_ui_rotary_click_freq(f);
    }

    // 값 변동 시 숫자/아이콘 업데이트
    if (prevMetVol != MetronomeVolume) {
        char buf[17];
        sprintf(buf, "%2d", MetronomeVolume);
        LCD16X2_Set_Cursor(MyLCD, 2, 1);
        LCD16X2_Write_String(MyLCD, buf);

        VolumeIcons_Draw(MetronomeVolume);
        prevMetVol = MetronomeVolume;
    }

    // 버튼1 → 순서대로 다음 단계로: (메트로놈 볼륨 → 전체 볼륨)
    if (buttonEvents[1] == BUTTON_EVENT_SHORT_PRESS) {
        buttonEvents[1] = BUTTON_EVENT_NONE;
        currentUIState = UI_MASTER_VOLUME;        // 다음 단계
        VolumeControlFirstRunFlag = 0;            // 다음 화면 진입시 1회 그리기 유도
        LastVolumeInteractionTick = VolumeTimer;
        return;
    }

    // 타임아웃/메뉴 조작 → 원래 화면 복귀 (5초 규칙 동일)
    if ((VolumeTimer - LastVolumeInteractionTick) >= 5000 ||
        rotaryEvent3 != ROTARY_EVENT_NONE ||
        buttonEvents[0] != BUTTON_EVENT_NONE ||
        buttonEvents[2] != BUTTON_EVENT_NONE) {

        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0] = BUTTON_EVENT_NONE;
        buttonEvents[2] = BUTTON_EVENT_NONE;

        currentUIState = PrevUIBeforeVolume;
        MetronomeVolumeFirstRunFlag = 0;
        return;
    }
}



//================================POWER RELATED===========================
static inline uint8_t ReadPowerSwitch(void) {
    return (HAL_GPIO_ReadPin(POWER_SW_GPIO_Port, POWER_SW_Pin) == GPIO_PIN_SET) ? 1 : 0;
}

// ───────────── SLEEP UI helpers ─────────────
// ───────────── SLEEP UI ─────────────
static void SleepUI_Init(void)
{
    // 1) 백라이트 OFF + 화면 싹 지우기
    LCDColorSet(7);                 // 프로젝트에서 7=백라이트 OFF
    LCD16X2_Clear(MyLCD);

    // 2) ModeSelection과 동일 포맷(° 커스텀 포함)으로 헤더 템플릿
    BatteryTemp_DrawStaticHeader(); // "00.0 C 00%" + 5열에 ° 커스텀 오버레이
    LCD16X2_Set_Cursor(MyLCD, 1, 10); LCD16X2_Write_Char(MyLCD, ' '); // 10열 '%' 지움
    BatteryTemp_HeaderMarkDirty();  // 첫 업데이트에서 숫자 강제 갱신
}

static void SleepUI_UpdateOnce(void)
{
    // DHT 드라이버가 HAL_Delay를 쓰므로, 잠깐 SysTick 재개
    //HAL_ResumeTick();
    g_dht_req = 1;
    Measure_Service();              // 온/습도 갱신 (DHT=TIM7 사용)
    BatteryTemp_HeaderService();    // 1행 숫자칸만 갱신(°/포맷/커스텀 고정)
    //HAL_SuspendTick();              // 다시 저전력
}

// ────────── 파워다운/업(온도/LCD 제외 전부) ──────────
static void Sleep_PowerDown_Periphs(void)
{
    // 오디오/I2S 정지 (DMA 포함)
    notch_stop();
    HAL_I2S_DeInit(&hi2s2);
    HAL_I2S_DeInit(&hi2s3);

    // UI/효과/버튼/배터리 관련 타이머 정지 (DHT=TIM7, 주기=TIM8은 유지)
    HAL_TIM_Base_Stop_IT(&htim5);                     // UI blink 등
    HAL_TIM_Base_Stop_IT(&htim6);                     // 버튼 스캐너 1kHz
    HAL_TIM_Base_Stop_IT(&htim3);                     // 배터리 ADC 트리거
    HAL_TIM_Encoder_Stop(&htim1, TIM_CHANNEL_ALL);    // 로터리
    HAL_TIM_Encoder_Stop(&htim4, TIM_CHANNEL_ALL);

    // ADC도 혹시 돌고 있으면 스톱
    HAL_ADC_Stop_IT(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // SysTick으로 깨어나는 빈도 줄이기
    //HAL_SuspendTick();
}

static void Sleep_PowerUp_Periphs(void)
{
    // SysTick 복구
    //HAL_ResumeTick();

    // I2S 재초기화 + 오디오 재시작
    MX_I2S2_Init();
    MX_I2S3_Init();
    notch_init(48000U);
    if (notch_start() != HAL_OK) { Error_Handler(); }

    // 타이머 원복 (원래 돌던 것들)
    HAL_TIM_Base_Start_IT(&htim3);                    // 배터리 ADC
    HAL_TIM_Base_Start_IT(&htim5);                    // UI blink
    HAL_TIM_Base_Start_IT(&htim6);                    // 버튼 스캐너 1kHz
    HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);   // 로터리
    HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);   // 로터리

    // (TIM7/DHT, TIM8은 SLEEP 동안 계속 유지됨)
}


// -------- SLEEP 기반 PowerSave 진입 --------
// -------- SLEEP 기반 PowerSave 진입 --------
void PowerSave_Start(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
    if (g_ps_active) return;
    ModeSelectionFirstRunFlag = 0;
    currentUIState = UI_MODE_SELECTION;


    g_ps_active = 1;
    // LCD 백라이트 OFF + 화면 클리어 + 헤더 템플릿
    SleepUI_Init();

    // 온/습도를 제외한 것들 전부 끄기
    Sleep_PowerDown_Periphs();

    // TIM8(≈1분) 주기 타이머 시작
    __HAL_TIM_SET_COUNTER(&htim8, 0);
    HAL_TIM_Base_Start_IT(&htim8);

    // 진입 즉시 1회 업데이트(선택) → 백라이트는 계속 OFF
    SleepUI_UpdateOnce();
    g_ps_tick = 0;
}

void PowerSave_Stop(void)
{
    if (!g_ps_active) return;
    g_ps_active = 0;

    // 1분 타이머 정지
    HAL_TIM_Base_Stop_IT(&htim8);

    // 모든 주변장치 원복
    Sleep_PowerUp_Periphs();

    // 화면 색상은 운영 로직에 맡김(ModeSelection 유지)
}


void Power_Task(void)
{
    uint8_t level = ReadPowerSwitch();  // 1=ON, 0=OFF
    uint32_t now  = HAL_GetTick();

    // 간단 디바운스
    static uint8_t last_read = 1;
    if (level != last_read) {
        g_sw_last_tick = now;           // 변화 감지
        last_read = level;
    }
    if ((now - g_sw_last_tick) < SW_DB_MS) return; // 안정화 대기

    // 안정화된 입력과 현재 전원 상태 비교해 분기
    if (level != g_sw_stable) {
        g_sw_stable = level;

        if (g_sw_stable == 0) {
            // 스위치 OFF → SLEEP 기반 PowerSave 진입
         ModeSelectionFirstRunFlag      = 0;
        	PracticeHomeFirstRunFlag       = 0;
        	TunerHomeFirstRunFlag          = 0;
        	MetronomeHomeFirstRunFlag      = 0;
        	SoundGenHomeFirstRunFlag       = 0;
        	SettingsHomeFirstRunFlag       = 0;
        	VolumeControlFirstRunFlag      = 0;
        	BalanceControlFirstRunFlag     = 0;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);

            LEDModeSet(0);

            PowerSave_Start();
        } else {
            // 스위치 ON  → SLEEP 기반 PowerSave 탈출
            PowerSave_Stop();
        }
    }
}


///////// 백업아 살아있냐?
/* ====== 부팅 시 BKP SRAM 상태 확인 + 복구 워크플로 ====== */
static void Boot_RTC_Setup_Screen(void)
{
	 Clock_UI_SetTime();
}

static void Boot_Backup_CheckSequence(void)
{
    uint8_t bkp_ok;

    /* BKP SRAM 전원/접근 활성화 + 읽기 시도 */
    Powersave_Bkpsram_Init();
    bkp_ok = Powersave_Bkpsram_LoadAndApply();

    if (bkp_ok) {
        /* 살아있으면 조용히 빠짐 */
        return;
    }

    /* ─ 1) 깨졌다고 알리는건 뺏어요─ */

    /* ─ 2) RTC 설정 스텁 실행 ─ */

    /* ─ 3) 플래시 복구 여부 묻기 ─ */
    {
        uint8_t sel = 0;   /* 0: YES, 1: NO */

        /* 입력 버퍼 싹 비우고 시작 */
        rotaryEvent3 = ROTARY_EVENT_NONE;
        buttonEvents[0] = buttonEvents[1] = buttonEvents[2] = BUTTON_EVENT_NONE;

        while (1) {
            /* 화면 그리기 */
            LCD16X2_Clear(MyLCD);
            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            LCD16X2_Write_String(MyLCD, "Recover Data? :(");
            LCD16X2_Set_Cursor(MyLCD, 2, 1);
            if (sel == 0) {
                LCD16X2_Write_String(MyLCD, ">YES<  NO ");
            } else {
                LCD16X2_Write_String(MyLCD, " YES  >NO<");
            }

            /* 여기서부터는 사용자가 선택할 때까지 루프 */
            while (1) {
                /* ─ 로터리 3번(=htim4) 읽어서 토글 ─ */
                Poll_Rotary(&htim4, &prev_count4, (RotaryEvent*)&rotaryEvent3);
                if (rotaryEvent3 == ROTARY_EVENT_CW ||
                    rotaryEvent3 == ROTARY_EVENT_CCW) {
                    sel ^= 1;                   /* YES ↔ NO */
                    rotaryEvent3 = ROTARY_EVENT_NONE;
                    notch_ui_button_beep();
                    break;                      /* 화면 다시 그리러 위로 */
                }

                /* ─ 엔터(버튼 2) 눌렀으면 결정 ─ */
                if (buttonEvents[2] == BUTTON_EVENT_SHORT_PRESS) {
                    buttonEvents[2] = BUTTON_EVENT_NONE;
                    notch_ui_button_beep();
                    goto decide_recover;
                }

                HAL_Delay(5);
            }
        }

decide_recover:
        if (sel == 0) {
        	ConfigStorage_Service(0);
        } else {
            /* NO → 그냥 넘어감 */
            LCD16X2_Clear(MyLCD);
            LCD16X2_Set_Cursor(MyLCD, 1, 1);
            LCD16X2_Write_String(MyLCD, "SYSTEM RESET..");
            HAL_Delay(1000);
        }
    }

    Boot_RTC_Setup_Screen();

}









/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2S2_Init();
  MX_I2S3_Init();
  MX_TIM2_Init();
  MX_TIM4_Init();
  MX_TIM5_Init();
  MX_TIM6_Init();
  MX_TIM1_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  MX_TIM7_Init();
  MX_TIM8_Init();
  MX_RTC_Init();
  MX_TIM9_Init();
  MX_TIM12_Init();
  /* USER CODE BEGIN 2 */

  LCD16X2_Init(MyLCD);

  LCD16X2_Clear(MyLCD);
  DELAY_US (1000);


  LCDColorSet(6);
  g_sw_stable    = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_SET;
  last_read      = g_sw_stable;
  g_sw_last_tick = HAL_GetTick();



   HAL_Delay(10);


   __HAL_TIM_SET_COUNTER(&htim1, 0);
   __HAL_TIM_SET_COUNTER(&htim4, 0);
   HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);
   HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);
   __HAL_TIM_SET_COUNTER(&htim2, 0);
   __HAL_TIM_SET_COUNTER(&htim3, 0);
   HAL_TIM_Base_Start_IT(&htim3);
   HAL_TIM_Base_Start_IT(&htim5);
   HAL_TIM_Base_Start_IT(&htim6);
   HAL_TIM_Base_Start_IT(&htim7);
   Power_Task();

   BatteryTemp_DHT_Init();     // ← DHT 구동 준비
   notch_init(48000U);                 // I2S2 AudioFreq=48k에 맞춤

   if (notch_start() != HAL_OK) { Error_Handler(); }
   BootCheck_FlashDebug();
   BootCheck_TestMode();
   //BootCheck_FWUpdate();
   BootCheck_RESETMode();

   SCB->CCR |= SCB_CCR_STKALIGN_Msk; // // 예외 진입 시 스택 8바이트 정렬 보장
   // (선택) 개발 중엔 언얼라인드 트랩 끄기: 하위 레거시 루틴 보호
   // SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;

   // 백업 준비
   Powersave_Bkpsram_Init();
   // 야 백업아 살아있냐?
   /* 여기! 슈퍼루프 들어가기 전에 한 번만 */
   Boot_Backup_CheckSequence();

   LED_Init();

   ccm_overlay_boot_select(TestModeEnabled);
   if (TestModeEnabled) {
       TestMode_RunLoop();   // 필요 시 내부 종료 조건에서 return 가능
   }
   HAL_ADC_Start_IT(&hadc1);


   //Batteries_MeasureAndDisplay3s();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  // 예: Practice 화면 루프 끝, 또는 공용 RenderUI 끝 부분
	  Powersave_PumpSaveIfNeeded();


	  Power_Task();
	  if (g_ps_active) {
	      if (g_ps_wake) {
	          g_ps_wake = 0;
	          // 1회 온습도 측정 + 1행 출력 (백라이트는 OFF 상태 유지)
	          SleepUI_UpdateOnce();
	      }
	      __WFI();        // 인터럽트 대기 → 저전력 유지
	      continue;       // 일반 UI/오디오 루프를 건너뜀
	  }

	  Poll_Rotary(&htim1, &prev_count3, &rotaryEvent2);
	  Poll_Rotary(&htim4, &prev_count4, &rotaryEvent3);

	  // [REPLACE THIS BLOCK]
	  if (rotaryEvent2 != ROTARY_EVENT_NONE &&
	      currentUIState != UI_MASTER_VOLUME &&
	      currentUIState != UI_SOUND_BALANCE &&
	      currentUIState != UI_METRONOME_VOLUME)   // ← 메트 볼륨 오버레이 중복 진입 방지
	  {
	      // Practice 홈 + VU가 돌고 있을 때는 화면 오버레이 금지 (기존 동작 유지)
	      if (currentUIState == UI_PRACTICE_HOME && g_vu_active) {
	          if (rotaryEvent2 == ROTARY_EVENT_CW && MasterVolume < 50) {
	              MasterVolume++;
	          } else if (rotaryEvent2 == ROTARY_EVENT_CCW && MasterVolume > 0) {
	              MasterVolume--;
	          }
	          rotaryEvent2 = ROTARY_EVENT_NONE; // 이벤트 소거
	          // 상태는 그대로 유지 (오버레이 진입 금지)
	      } else {
	          // 평소처럼 오버레이 진입하되,
	          // 메트로놈 홈일 때만 '메트로놈 볼륨' 오버레이부터 진입
	          VolumeTimer = 0;
	          PrevUIBeforeVolume = currentUIState;

	          if (currentUIState == UI_METRONOME_HOME) {
	              currentUIState = UI_METRONOME_VOLUME;   // ← 변경점
	              MetronomeVolumeFirstRunFlag = 0;        // 1회 그리기 유도
	          } else {
	              currentUIState = UI_MASTER_VOLUME;      // 기존 동작
	              VolumeControlFirstRunFlag = 0;
	          }

	          LastVolumeInteractionTick = VolumeTimer;
	          rotaryEvent2 = ROTARY_EVENT_NONE;
	      }
	  }



	  RenderUI();
	  notch_task();
	  Measure_Service();
	  Battery_CheckAndShutdownIfLow_Blocking();

	  TunerUI_PumpFromMain();

	  if (g_tuner_ui_on){
	      Tuner_Task();
	  }

	  OperationLEDSet(0);

	  //LEDStrip_Update(1);

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
  PeriphClkInitStruct.PLLI2S.PLLI2SN = 344;
  PeriphClkInitStruct.PLLI2S.PLLI2SR = 7;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2S2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S2_Init(void)
{

  /* USER CODE BEGIN I2S2_Init 0 */

  /* USER CODE END I2S2_Init 0 */

  /* USER CODE BEGIN I2S2_Init 1 */

  /* USER CODE END I2S2_Init 1 */
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_24B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_ENABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S2_Init 2 */

  /* USER CODE END I2S2_Init 2 */

}

/**
  * @brief I2S3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S3_Init(void)
{

  /* USER CODE BEGIN I2S3_Init 0 */

  /* USER CODE END I2S3_Init 0 */

  /* USER CODE BEGIN I2S3_Init 1 */

  /* USER CODE END I2S3_Init 1 */
  hi2s3.Instance = SPI3;
  hi2s3.Init.Mode = I2S_MODE_MASTER_RX;
  hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s3.Init.DataFormat = I2S_DATAFORMAT_24B;
  hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s3.Init.CPOL = I2S_CPOL_LOW;
  hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S3_Init 2 */

  /* USER CODE END I2S3_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */
  Clock_Init();
  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
  if (Clock_IsTimeValid()) {
      // 이미 사용자가 시간 맞춘 보드니까 아래 Cube 기본 시/날짜 세팅은 건너뛰기
      return;
  }

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 84-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 0xFFFFFFFF;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 41999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim4, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief TIM5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM5_Init(void)
{

  /* USER CODE BEGIN TIM5_Init 0 */

  /* USER CODE END TIM5_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM5_Init 1 */

  /* USER CODE END TIM5_Init 1 */
  htim5.Instance = TIM5;
  htim5.Init.Prescaler = 1;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = 41999;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim5, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM5_Init 2 */

  /* USER CODE END TIM5_Init 2 */
  HAL_TIM_MspPostInit(&htim5);

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 8400-1;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 9;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief TIM7 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM7_Init(void)
{

  /* USER CODE BEGIN TIM7_Init 0 */

  /* USER CODE END TIM7_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM7_Init 1 */

  /* USER CODE END TIM7_Init 1 */
  htim7.Instance = TIM7;
  htim7.Init.Prescaler = 8749;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.Period = 9;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim7) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim7, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM7_Init 2 */

  /* USER CODE END TIM7_Init 2 */

}

/**
  * @brief TIM8 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM8_Init(void)
{

  /* USER CODE BEGIN TIM8_Init 0 */

  /* USER CODE END TIM8_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM8_Init 1 */

  /* USER CODE END TIM8_Init 1 */
  htim8.Instance = TIM8;
  htim8.Init.Prescaler = 16799;
  htim8.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim8.Init.Period = 9999;
  htim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim8.Init.RepetitionCounter = 60;
  htim8.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim8) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim8, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim8, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM8_Init 2 */

  /* USER CODE END TIM8_Init 2 */

}

/**
  * @brief TIM9 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM9_Init(void)
{

  /* USER CODE BEGIN TIM9_Init 0 */

  /* USER CODE END TIM9_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM9_Init 1 */

  /* USER CODE END TIM9_Init 1 */
  htim9.Instance = TIM9;
  htim9.Init.Prescaler = 3;
  htim9.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim9.Init.Period = 41999;
  htim9.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim9.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim9) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim9, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim9) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim9, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim9, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM9_Init 2 */

  /* USER CODE END TIM9_Init 2 */
  HAL_TIM_MspPostInit(&htim9);

}

/**
  * @brief TIM12 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM12_Init(void)
{

  /* USER CODE BEGIN TIM12_Init 0 */

  /* USER CODE END TIM12_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM12_Init 1 */

  /* USER CODE END TIM12_Init 1 */
  htim12.Instance = TIM12;
  htim12.Init.Prescaler = 1;
  htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim12.Init.Period = 41999;
  htim12.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim12) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim12, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim12, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM12_Init 2 */

  /* USER CODE END TIM12_Init 2 */
  HAL_TIM_MspPostInit(&htim12);

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 8, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
  /* DMA1_Stream4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 8, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, B1_Pin|B2_Pin|B3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, D4_Pin|D5_Pin|D6_Pin|GPIO_PIN_3
                          |RS_Pin|E_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, POWER_led_Pin|BUSY_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PE2 PE3 PE4 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : B1_Pin B2_Pin B3_Pin */
  GPIO_InitStruct.Pin = B1_Pin|B2_Pin|B3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PE14 */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PA9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : D4_Pin D5_Pin D6_Pin PD3
                           RS_Pin E_Pin */
  GPIO_InitStruct.Pin = D4_Pin|D5_Pin|D6_Pin|GPIO_PIN_3
                          |RS_Pin|E_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB6 */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : POWER_led_Pin BUSY_LED_Pin */
  GPIO_InitStruct.Pin = POWER_led_Pin|BUSY_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  HAL_NVIC_SetPriority(EXTI3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {

	static uint16_t s_blink_ms = 0, s_adc2s_ms = 0;

    if (htim->Instance == TIM1) {
    }

    if (htim->Instance == TIM6) {
        LED_SD_1kHz_ISR();
        Buttons_Scan_1ms();
        VolumeTimer++;

        if (++s_blink_ms >= 500) {  // 0.5초
            s_blink_ms = 0;
            UITotalBlinkStatus ^= 1;

        }

        if (++s_adc2s_ms >= 2000) { // 2초
            s_adc2s_ms = 0;
            #if USE_ADC_DMA
              HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&s_adc_dma_buf, 1);
            #else
              HAL_ADC_Start_IT(&hadc1);
            #endif
            if (currentUIState == UI_MODE_SELECTION) g_dht_req = 1;
        }


        // (선택) 모두 놓임 + 일정시간 정지하고 싶다면 다음 라인 주석 해제해서 조건부 정지 가능
        // if (!btn_stable[0] && !btn_stable[1] && !btn_stable[2]) HAL_TIM_Base_Stop_IT(&htim6);
    }


    if (htim->Instance == TIM8) {
        if (g_ps_active) g_ps_wake = 1;  // g_ps_tick 대신 g_ps_wake 사용
    }

    if (htim->Instance == TIM7) {
        LED_Tick_120Hz_ISR();

        // 튜너 화면 켜져 있을 때만 깨움
        if (g_tuner_ui_on) {
            Tuner_Tick_10ms_fromISR();
        }
    }

}

//교체된 콜백 함수 (버튼 딜레이 잡기 위해서)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // PE2/3/4만 취급: 버튼 인터럽트는 타이머 스캐너를 '깨우는' 역할만 한다
    if (GPIO_Pin == GPIO_PIN_2 || GPIO_Pin == GPIO_PIN_3 || GPIO_Pin == GPIO_PIN_4) {
        __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
        HAL_TIM_Base_Start_IT(&htim6);

    }

    BatteryTemp_OnExti(GPIO_Pin);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
#if !USE_ADC_DMA
    if (hadc->Instance == ADC1) {
        MainBattADC = (uint16_t)HAL_ADC_GetValue(hadc);
        HAL_ADC_Stop(hadc);           // IT 사용 시 Stop은 이걸로 충분
    }
#else
    // DMA 모드에서도 이 콜백이 들어온다(HAL이 ConvCplt에서 불러줌)
    if (hadc->Instance == ADC1) {
        MainBattADC = s_adc_dma_buf;
        HAL_ADC_Stop_DMA(hadc);       // 다음 20초까지 DMA 릴리즈
    }
#endif
}




/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
