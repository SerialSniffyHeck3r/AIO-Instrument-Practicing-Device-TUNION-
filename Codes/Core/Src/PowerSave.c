/* =====================================================================
 * Powersave.c
 * - STM32F407VGT6 BKP SRAM(4KB, VBAT 유지) 에 PowerFlash와 동일한
 *   설정 스냅샷을 한 벌 더 저장하는 유틸
 *
 * 사용법:
 *   1) 부팅 직후 한 번:
 *        Powersave_Bkpsram_Init();
 *
 *   2) 특정 메뉴 진입 시점(넌 여기서만 저장한다고 했지):
 *        Powersave_Bkpsram_SaveNow();
 *
 *   3) 부팅 시 “혹시 살아있나?” 확인해서 전역에 꽂기:
 *        if (!Powersave_Bkpsram_LoadAndApply()) {
 *            // 여기서 너가 "RTC 설정 + Flash 복구할래?" UI 띄우면 됨
 *        }
 *
 * 다른 기존 코드 안 고침.
 * ===================================================================== */

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdint.h>
#include "main.h"

/* BKP SRAM 베이스 - 보드에 따라 이미 정의돼 있을 수 있음 */
#ifndef BKPSRAM_BASE
#define BKPSRAM_BASE  (0x40024000UL)
#endif

/* 마법값/버전 */
#define PS_MAGIC    (0xDEADBEEFUL)   /* 'B','S','M','C' 그냥 임의 */
#define PS_VERSION  (0x0001u)

/* ---------------------------------------------------------------------
 * 여기부터는 PowerFlash.c에서 쓰던 전역 변수들을 그대로 extern
 * --------------------------------------------------------------------- */
extern uint16_t MasterVolume;
extern uint16_t SoundBalance;
extern uint16_t SFXVolume;
extern uint8_t  CurrentNoteIndex;
extern uint32_t TunerCalibrationValue;

extern uint32_t CutOffFreqStart, CutOffFreqEnd;
extern uint32_t CutOffFreqStartUser1, CutOffFreqEndUser1;
extern uint32_t CutOffFreqStartUser2, CutOffFreqEndUser2;
extern uint32_t CutOffFreqStartUser3, CutOffFreqEndUser3;

extern int8_t   PitchSemitone;
extern volatile int8_t  MicBoost_dB;
extern volatile uint8_t MicAGC_On;
extern volatile uint16_t MicInputMode;
extern volatile uint8_t AutoVU_After10s;

extern uint16_t MetronomeBPM;
extern uint8_t  TimeSignature;
extern uint16_t TunerBaseFreq;
extern uint8_t  SoundGenMode;

/* main.c 전역과 매칭되는 Practice/Timer/UI 확장변수들 */
extern volatile uint32_t g_sw_total_ms;
extern volatile uint16_t g_timer_set_min;
extern volatile int32_t  g_timer_remaining_ms;
extern volatile uint32_t g_sw_now_ms;
extern volatile uint8_t  g_pract_menu_index;
extern uint8_t           CutOffOnOff;
extern uint8_t           g_timer_run;

extern volatile uint8_t  g_tnr_sens;
extern volatile float    Cal_RH_Offset, Cal_RH_Scale;
extern volatile float    Cal_TempC_Offset, Cal_TempC_Scale;
extern volatile uint8_t  TempUnitF;

/* enum 은 main.c 에만 있으므로 int 로 외부선언 돼 있었음 */
extern int CurrentInstrumentType;

/* ---------------------------------------------------------------------
 * PowerFlash.c 안에 있던 payload 구조체를 그대로 다시 선언
 * (이 파일 하나만으로 동작하도록)
 * --------------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    /* ===== 기존 필드 ===== */
    uint16_t MasterVolume;
    uint16_t SoundBalance;
    uint16_t SFXVolume;

    uint8_t  CurrentNoteIndex;
    int8_t   PitchSemitone;
    int8_t   MicBoost_dB;
    uint8_t  MicAGC_On;

    uint16_t MicInputMode;
    uint8_t  AutoVU_After10s;
    uint8_t  _pad0;

    uint32_t TunerCalibrationValue;

    uint32_t CutOffFreqStart, CutOffFreqEnd;
    uint32_t CutOffFreqStartUser1, CutOffFreqEndUser1;
    uint32_t CutOffFreqStartUser2, CutOffFreqEndUser2;
    uint32_t CutOffFreqStartUser3, CutOffFreqEndUser3;

    uint16_t MetronomeBPM;
    uint8_t  TimeSignature;
    uint16_t TunerBaseFreq;
    uint8_t  SoundGenMode;

    /* ===== Practice/Timer/UI 확장 ===== */
    uint32_t g_sw_total_ms;
    uint16_t g_timer_set_min;
    int32_t  g_timer_remaining_ms;
    uint32_t g_sw_now_ms;
    uint8_t  g_pract_menu_index;
    uint8_t  CutOffOnOff;
    uint8_t  CurrentInstrumentType;
    uint8_t  g_timer_run;

    /* ===== Sensor/Tuner 보정 ===== */
    uint8_t  g_tnr_sens;
    uint8_t  TempUnitF;
    uint32_t Cal_RH_Offset_u32;
    uint32_t Cal_RH_Scale_u32;
    uint32_t Cal_TempC_Offset_u32;
    uint32_t Cal_TempC_Scale_u32;

    uint8_t  _pad_tail[2];   /* 4B 정렬 맞추기 */
} FlashCfgPayload;
#pragma pack(pop)

/* BKP SRAM에 올려둘 실제 이미지 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
    FlashCfgPayload payload;
} PS_Image;

/* BKP SRAM 실제 위치 */
#define PS_IMAGE   ((PS_Image *)BKPSRAM_BASE)

/* ---------------------------------------------------------------------
 * 유틸 함수들 (PowerFlash랑 같은 CRC 써서 호환)
 * --------------------------------------------------------------------- */
static uint32_t ps_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (uint8_t b = 0; b < 8; ++b) {
            uint32_t m = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320UL & m);
        }
    }
    return ~crc;
}

static inline uint32_t ps_f2u(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}
static inline float ps_u2f(uint32_t u)
{
    union { uint32_t u; float f; } v;
    v.u = u;
    return v.f;
}

/* 지금 전역변수 상태를 FlashCfgPayload 한 벌로 뽑기 */
static void ps_build_snapshot(FlashCfgPayload *out)
{
    memset(out, 0, sizeof(*out));

    out->MasterVolume     = MasterVolume;
    out->SoundBalance     = SoundBalance;
    out->SFXVolume        = SFXVolume;

    out->CurrentNoteIndex = CurrentNoteIndex;
    out->PitchSemitone    = PitchSemitone;
    out->MicBoost_dB      = MicBoost_dB;
    out->MicAGC_On        = MicAGC_On;

    out->MicInputMode     = MicInputMode;
    out->AutoVU_After10s  = AutoVU_After10s;

    out->TunerCalibrationValue = TunerCalibrationValue;

    out->CutOffFreqStart       = CutOffFreqStart;
    out->CutOffFreqEnd         = CutOffFreqEnd;
    out->CutOffFreqStartUser1  = CutOffFreqStartUser1;
    out->CutOffFreqEndUser1    = CutOffFreqEndUser1;
    out->CutOffFreqStartUser2  = CutOffFreqStartUser2;
    out->CutOffFreqEndUser2    = CutOffFreqEndUser2;
    out->CutOffFreqStartUser3  = CutOffFreqStartUser3;
    out->CutOffFreqEndUser3    = CutOffFreqEndUser3;

    out->MetronomeBPM   = MetronomeBPM;
    out->TimeSignature  = TimeSignature;
    out->TunerBaseFreq  = TunerBaseFreq;
    out->SoundGenMode   = SoundGenMode;

    out->g_sw_total_ms        = g_sw_total_ms;
    out->g_timer_set_min      = g_timer_set_min;
    out->g_timer_remaining_ms = g_timer_remaining_ms;
    out->g_sw_now_ms          = g_sw_now_ms;
    out->g_pract_menu_index   = g_pract_menu_index;
    out->CutOffOnOff          = CutOffOnOff;
    out->CurrentInstrumentType= (uint8_t)CurrentInstrumentType;
    out->g_timer_run          = g_timer_run;

    out->g_tnr_sens           = g_tnr_sens;
    out->TempUnitF            = TempUnitF;
    out->Cal_RH_Offset_u32    = ps_f2u(Cal_RH_Offset);
    out->Cal_RH_Scale_u32     = ps_f2u(Cal_RH_Scale);
    out->Cal_TempC_Offset_u32 = ps_f2u(Cal_TempC_Offset);
    out->Cal_TempC_Scale_u32  = ps_f2u(Cal_TempC_Scale);
}

/* BKP SRAM에서 읽은 payload를 다시 전역 변수에 꽂기 */
static void ps_apply_to_globals(const FlashCfgPayload *pl)
{
    MasterVolume     = pl->MasterVolume;
    SoundBalance     = pl->SoundBalance;
    SFXVolume        = pl->SFXVolume;

    CurrentNoteIndex = pl->CurrentNoteIndex;
    PitchSemitone    = pl->PitchSemitone;
    MicBoost_dB      = pl->MicBoost_dB;
    MicAGC_On        = pl->MicAGC_On;

    MicInputMode     = pl->MicInputMode;
    AutoVU_After10s  = pl->AutoVU_After10s;

    TunerCalibrationValue = pl->TunerCalibrationValue;

    CutOffFreqStart       = pl->CutOffFreqStart;
    CutOffFreqEnd         = pl->CutOffFreqEnd;
    CutOffFreqStartUser1  = pl->CutOffFreqStartUser1;
    CutOffFreqEndUser1    = pl->CutOffFreqEndUser1;
    CutOffFreqStartUser2  = pl->CutOffFreqStartUser2;
    CutOffFreqEndUser2    = pl->CutOffFreqEndUser2;
    CutOffFreqStartUser3  = pl->CutOffFreqStartUser3;
    CutOffFreqEndUser3    = pl->CutOffFreqEndUser3;

    MetronomeBPM   = pl->MetronomeBPM;
    TimeSignature  = pl->TimeSignature;
    TunerBaseFreq  = pl->TunerBaseFreq;
    SoundGenMode   = pl->SoundGenMode;

    g_sw_total_ms        = pl->g_sw_total_ms;
    g_timer_set_min      = pl->g_timer_set_min;
    g_timer_remaining_ms = pl->g_timer_remaining_ms;
    g_sw_now_ms          = pl->g_sw_now_ms;
    g_pract_menu_index   = pl->g_pract_menu_index;
    CutOffOnOff          = pl->CutOffOnOff;
    CurrentInstrumentType= (int)pl->CurrentInstrumentType;
    g_timer_run          = pl->g_timer_run;

    g_tnr_sens        = pl->g_tnr_sens;
    TempUnitF         = pl->TempUnitF;
    Cal_RH_Offset     = ps_u2f(pl->Cal_RH_Offset_u32);
    Cal_RH_Scale      = ps_u2f(pl->Cal_RH_Scale_u32);
    Cal_TempC_Offset  = ps_u2f(pl->Cal_TempC_Offset_u32);
    Cal_TempC_Scale   = ps_u2f(pl->Cal_TempC_Scale_u32);
}

/* ---------------------------------------------------------------------
 * 공개 함수들
 * --------------------------------------------------------------------- */

/* 1) BKP SRAM 접근/유지 켜기 */
void Powersave_Bkpsram_Init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    __HAL_RCC_BKPSRAM_CLK_ENABLE();

    // ★ 여기! 조건 빼고 무조건 호출
    HAL_PWREx_EnableBkUpReg();

    // 옵션: 레귤레이터 준비될 때까지 대기
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_BRR) == RESET) {
        ;   // nop
    }
}

/* 2) 지금 전역 설정을 BKP SRAM 에 덮어쓰기 */
void Powersave_Bkpsram_SaveNow(void)
{
    PS_Image img;

    img.magic   = PS_MAGIC;
    img.version = PS_VERSION;
    img.size    = (uint32_t)sizeof(FlashCfgPayload);

    ps_build_snapshot(&img.payload);
    img.crc32 = ps_crc32(&img.payload, sizeof(FlashCfgPayload));

    /* 그냥 메모리 복사면 끝 – 플래시처럼 지우고 이런 거 없음 */
    memcpy((void *)PS_IMAGE, &img, sizeof(PS_Image));

    /* 혹시모를 write buffer flush */
    __DSB();
    __ISB();
}

/* 3) BKP SRAM 내용이 온전하면 전역에 반영하고 1, 아니면 0 리턴 */
uint8_t Powersave_Bkpsram_LoadAndApply(void)
{
    const PS_Image *img = PS_IMAGE;

    if (img->magic   != PS_MAGIC)   return 0;
    if (img->version != PS_VERSION) return 0;
    if (img->size    != sizeof(FlashCfgPayload)) return 0;

    uint32_t crc = ps_crc32(&img->payload, sizeof(FlashCfgPayload));
    if (crc != img->crc32) return 0;

    /* 여기까지 오면 살아있음 → 전역에 적용 */
    ps_apply_to_globals(&img->payload);
    return 1;
}

/* 4) 일부러 무효화하고 싶을 때 (둘 다 뽑은 상황 흉내낼 때) */
void Powersave_Bkpsram_Invalidate(void)
{
    PS_IMAGE->magic = 0x00000000u;
    __DSB();
    __ISB();
}







// PowerSave.c (하단 or 적당한 위치)

// 저장 요청 플래그 (ISR/메인 공용)
volatile uint8_t g_ps_save_req = 0;

// 외부에서 호출: “지금 한 번 저장해줘”
void Powersave_RequestSave(void) { g_ps_save_req = 1; }

// 메인 콘텍스트에서만 실제 저장 (쿨다운으로 남발 억제)
void Powersave_PumpSaveIfNeeded(void)
{
    if (!g_ps_save_req) return;

    static uint32_t s_last_ms = 0;
    uint32_t now = HAL_GetTick();
    if ((now - s_last_ms) < 1200) return;   // 1.2초 쿨다운

    g_ps_save_req = 0;          // 소비
    s_last_ms = now;
    Powersave_Bkpsram_SaveNow();
}


