/* ================== STM32F407VGT6 Config Flash Storage (Wear-Leveling) ==================
 * - Sector: FLASH_SECTOR_11 @ 0x080E0000 (128KB)
 * - Single public API: ConfigStorage_Service(uint8_t trigger_save)
 *   * first-call with trigger_save=0 BEFORE while(1): scan & load last valid record to globals
 *   * whenever settings changed: call with trigger_save=1 -> writes only if changed vs shadow
 * ======================================================================================= */

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdint.h>

#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#define ARM_MATH_CM4
#include <stdio.h>
#include "Util.h"
#include "LCD16X2.h"
#include "LCD16X2_cfg.h"
#include "notch.h"




#include <math.h>
#include <stdlib.h> // abs()
#include <arm_math.h>


extern uint8_t ModeSelectionFirstRunFlag, PracticeHomeFirstRunFlag, TunerHomeFirstRunFlag,
               MetronomeHomeFirstRunFlag, SoundGenHomeFirstRunFlag, SettingsHomeFirstRunFlag,
               VolumeControlFirstRunFlag, BalanceControlFirstRunFlag;

// Practice/Timer/UI 확장 변수들 (main.c 전역과 매칭)
extern volatile uint32_t g_sw_total_ms;     // 누계 스톱워치
extern volatile uint16_t g_timer_set_min;   // 설정 분
extern volatile int32_t  g_timer_remaining_ms;
extern volatile uint32_t g_sw_now_ms;       // 현재 스톱워치
extern volatile uint8_t  g_pract_menu_index;
extern uint8_t           CutOffOnOff;       // 0/1/2 (main.c 전역)
extern uint8_t           g_timer_run;       // 1/0

extern volatile uint8_t g_tnr_sens;
extern volatile float   Cal_RH_Offset, Cal_RH_Scale;
extern volatile float   Cal_TempC_Offset, Cal_TempC_Scale;
extern volatile uint8_t TempUnitF;

// enum 정의는 main.c에만 있으므로 여기서는 'int'로 외부선언 후, u8↔int 캐스팅으로 사용
extern int CurrentInstrumentType;

extern volatile uint8_t FlashDebug;   // ← main.c 전역 사용 

// LCD 색상: main.c의 LCDColorSet() 매핑 준수 (3=G,5=R,4=B,2=SKY)
#define PF_COLOR_OK     3   // Green: 성공
#define PF_COLOR_ERR    5   // Red:   실패
#define PF_COLOR_INFO   4   // Blue:  정보(진행/상세)

// ★ 딜레이 파라미터 (원하면 #undef 후 재정의 가능)
#ifndef PF_DELAY_STEP_MS
#define PF_DELAY_STEP_MS   200U   // 스텝(진행 표시) 갱신마다 대기
#endif

#ifndef PF_DELAY_SAVED_MS
#define PF_DELAY_SAVED_MS  1000U  // SAVED / SAVE FAIL 토스트 유지 시간
#endif

#ifndef PF_DELAY_QUIET_MS
#define PF_DELAY_QUIET_MS  500U   // ★ FlashDebug==0일 때 모든 토스트 공통 유지시간
#endif








/* ---- Flash 영역 설정 (STM32F407VGT6) ---- */
#define CFG_FLASH_BASE_ADDR  (0x080A0000u)   // Sector 9
#define CFG_FLASH_SIZE        (128UL * 1024UL)
#define CFG_FLASH_END_ADDR   (0x08100000u)   // ★ exclusive (Sector 12 base, 없지만 범위 끝)
#define CFG_FLASH_SECTOR      (FLASH_SECTOR_11)
#define CFG_SECTOR_SIZE_128K (0x20000u)      // 128 KB

static inline uint32_t pf_sector_base_of(uint32_t addr) {
    // 9~11만 대상이므로 128KB 정렬만 써도 됨
    uint32_t off = (addr - CFG_FLASH_BASE_ADDR) / CFG_SECTOR_SIZE_128K;
    if (off > 2u) off = 2u;
    return CFG_FLASH_BASE_ADDR + off * CFG_SECTOR_SIZE_128K;
}
static inline uint32_t pf_sector_end_of(uint32_t addr) {
    uint32_t b = pf_sector_base_of(addr);
    return b + CFG_SECTOR_SIZE_128K; // exclusive
}
static inline uint32_t pf_next_sector_base(uint32_t addr) {
    uint32_t b = pf_sector_base_of(addr);
    uint32_t n = b + CFG_SECTOR_SIZE_128K;
    if (n >= CFG_FLASH_END_ADDR) n = CFG_FLASH_BASE_ADDR; // ring wrap
    return n;
}

// HAL 지우기용 섹터 번호(FLASH_SECTOR_9~11)
static inline uint32_t pf_sector_index_of(uint32_t addr) {
    uint32_t b = pf_sector_base_of(addr);
    if (b >= 0x080E0000u) return FLASH_SECTOR_11;
    if (b >= 0x080C0000u) return FLASH_SECTOR_10;
    return FLASH_SECTOR_9;
}

/* ---- 레코드 포맷 ----
   [Header]
     u32 magic = 0x43534731 ('CSG1')
     u32 version = 0x00010001
     u32 seq      (단조증가)
     u32 payload_size (bytes, 4바이트 정렬)
     u32 crc32 (payload에 대한 CRC)
   [Payload] = 설정 구조체(4바이트 정렬)   */
#define CFG_MAGIC         (0x43534731UL)  // 'C''S''G''1'
#define CFG_VERSION       (0x00010004UL)    // ★ 새 구조 버전

// 스캐너 호환 테이블
#define CFG_VERSION_CUR   (0x00010004UL)
#define CFG_VERSION_OLD1  (0x00010003UL)
#define CFG_VERSION_OLD0  (0x00010002UL)

#pragma pack(push,1)
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

    /* ===== 새로 추가되는 필드들(Practice/Timer/UI) ===== */
    uint32_t g_sw_total_ms;         // 누계 스톱워치 ms
    uint16_t g_timer_set_min;       // 타이머 설정 분
    int32_t  g_timer_remaining_ms;  // 남은 시간 ms (<0 비활성)
    uint32_t g_sw_now_ms;           // 현재 스톱워치 ms
    uint8_t  g_pract_menu_index;    // 프랙티스 메뉴 인덱스
    uint8_t  CutOffOnOff;           // 0/1/2
    uint8_t  CurrentInstrumentType; // enum 저장용 0..Instrument_MAX-1
    uint8_t  g_timer_run;           // 1: 러닝, 0: 정지

    /* ====== ★ 추가: Sensor/Tuner 보정값 & 설정 ====== */
    uint8_t  g_tnr_sens;            // 저장: u8
    uint8_t  TempUnitF;             // 0=C, 1=F

    uint32_t Cal_RH_Offset_u32;     // float 비트 저장
    uint32_t Cal_RH_Scale_u32;
    uint32_t Cal_TempC_Offset_u32;
    uint32_t Cal_TempC_Scale_u32;

    uint8_t  _pad_tail[2];          // 4B 정렬 보정(총 크기 4의 배수)
} FlashCfgPayload;
#pragma pack(pop)



typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t seq;
    uint32_t payload_size;  // 4바이트 정렬
    uint32_t crc32;         // payload only
    // followed by payload bytes...
} FlashCfgHeader;









/* ---- 메모리에 갖다 쳐박을거!!!!! ---- */
extern uint16_t MasterVolume;             // 0..50 (감마 LUT elsewhere)
extern uint16_t SoundBalance;             // 0..50
extern uint16_t SFXVolume;                // 0..50 (4단계 맵핑은 UI에서)
extern uint8_t  CurrentNoteIndex;         // 튜너 A4 기반
extern uint32_t TunerCalibrationValue;

extern uint32_t CutOffFreqStart, CutOffFreqEnd;
extern uint32_t CutOffFreqStartUser1, CutOffFreqEndUser1;
extern uint32_t CutOffFreqStartUser2, CutOffFreqEndUser2;
extern uint32_t CutOffFreqStartUser3, CutOffFreqEndUser3;

extern int8_t   PitchSemitone;            // -? .. ?
extern volatile int8_t  MicBoost_dB;      // -12..+12  (저장/복원만 할 뿐, 런타임 사용은 기존대로)
extern volatile uint8_t MicAGC_On;        // 0/1
extern volatile uint16_t MicInputMode;    // 0,1,2
extern volatile uint8_t AutoVU_After10s;  // 0/1

extern uint16_t MetronomeBPM;     // 예: 130
extern uint8_t  TimeSignature;    // 예: 4
extern uint16_t TunerBaseFreq;    // 예: 440
extern uint8_t  SoundGenMode;     // 예: 0

static inline uint32_t f2u(float f){ union{ float f; uint32_t u; } v; v.f=f; return v.u; }
static inline float    u2f(uint32_t u){ union{ uint32_t u; float f; } v; v.u=u; return v.f; }


#define CFG_FLASH_DEBUG 1

#define MyLCD LCD16X2_1
extern void LCDColorSet(uint8_t c);

extern uint8_t UI_AllowFlashToast(void);
extern void    UI_RedrawModeSelectionSoon(void);



// ----- Blank(0xFF..) 검사: addr ~ addr+len-1 이 전부 0xFF면 1, 아니면 0
static int cfg_is_blank(uint32_t addr, uint32_t len) {
    for (uint32_t off = 0; off < len; off += 4) {
        if (*(volatile uint32_t *)(addr + off) != 0xFFFFFFFFu) return 0;
    }
    return 1;
}

// 현재 전역 설정 → FlashCfgPayload 스냅샷(패딩 0 보장)

static inline void cfg_build_snapshot(FlashCfgPayload *out) {
    memset(out, 0, sizeof(*out));              // ★ 패딩/미사용 필드 0으로
    out->MasterVolume          = MasterVolume;
    out->SoundBalance          = SoundBalance;
    out->SFXVolume             = SFXVolume;

    out->CurrentNoteIndex      = CurrentNoteIndex;
    out->PitchSemitone         = PitchSemitone;
    out->MicBoost_dB           = MicBoost_dB;
    out->MicAGC_On             = MicAGC_On;

    out->MicInputMode          = MicInputMode;
    out->AutoVU_After10s       = AutoVU_After10s;

    out->TunerCalibrationValue = TunerCalibrationValue;

    out->CutOffFreqStart       = CutOffFreqStart;
    out->CutOffFreqEnd         = CutOffFreqEnd;
    out->CutOffFreqStartUser1  = CutOffFreqStartUser1;
    out->CutOffFreqEndUser1    = CutOffFreqEndUser1;
    out->CutOffFreqStartUser2  = CutOffFreqStartUser2;
    out->CutOffFreqEndUser2    = CutOffFreqEndUser2;
    out->CutOffFreqStartUser3  = CutOffFreqStartUser3;
    out->CutOffFreqEndUser3    = CutOffFreqEndUser3;

    // 확장 필드
    out->MetronomeBPM          = MetronomeBPM;
    out->TimeSignature         = TimeSignature;
    out->TunerBaseFreq         = TunerBaseFreq;
    out->SoundGenMode          = SoundGenMode;

    out->g_sw_total_ms        = g_sw_total_ms;
    out->g_timer_set_min      = g_timer_set_min;
    out->g_timer_remaining_ms = g_timer_remaining_ms;
    out->g_sw_now_ms          = g_sw_now_ms;
    out->g_pract_menu_index   = g_pract_menu_index;
    out->CutOffOnOff          = CutOffOnOff;
    out->CurrentInstrumentType= (uint8_t)CurrentInstrumentType;
    out->g_timer_run          = g_timer_run;

    // ===== 새 필드 스냅샷 =====
    out->g_tnr_sens            = g_tnr_sens;
    out->TempUnitF             = TempUnitF;
    out->Cal_RH_Offset_u32     = f2u(Cal_RH_Offset);
    out->Cal_RH_Scale_u32      = f2u(Cal_RH_Scale);
    out->Cal_TempC_Offset_u32  = f2u(Cal_TempC_Offset);
    out->Cal_TempC_Scale_u32   = f2u(Cal_TempC_Scale);

}

// ★ 안전 딜레이: SysTick 기반(IRQ ON 전제). IRQ OFF 구간에서는 호출 금지!
static inline void pf_delay_ms(uint32_t ms) {
    uint32_t s = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - s) < ms) {
        __NOP();
    }
}



static inline void pf_lcd_line(uint8_t row, const char *txt) {
    char buf[17];
    uint8_t i=0;
    for (; i<16 && txt && txt[i]; ++i) buf[i] = txt[i];
    for (; i<16; ++i) buf[i] = ' ';
    buf[16] = '\0';
    LCD16X2_Set_Cursor(MyLCD, row, 1);
    LCD16X2_Write_String(MyLCD, buf);

}




// 축약 메시지 전용
static inline void pf_lcd_simple(const char *msg1, const char *msg2) {
    if (!FlashDebug) return;                // 런타임 제어
    LCDColorSet(PF_COLOR_INFO);             // 정보는 파랑(4)
    pf_lcd_line(1, msg1);
    pf_lcd_line(2, msg2);
}

static inline void pf_lcd_simple_color(const char *msg1, const char *msg2, uint8_t color) {
    LCDColorSet(color);
    pf_lcd_line(1, msg1);
    pf_lcd_line(2, msg2);
}

// 상세 메시지: 단계/주소 표시
static inline void pf_lcd_verbose_phase(const char *phase, uint32_t addr, uint32_t done, uint32_t total) {
    if (!FlashDebug) return;                // 런타임 제어
    char l1[17], l2[17];
    // 1행: "FLASH <PHASE>"
    snprintf(l1, sizeof(l1), "FLASH %-10.10s", phase);
    // 2행: A:xxxxxx D:yy/tt
    uint32_t off = (addr & 0x00FFFFF);
    snprintf(l2, sizeof(l2), "A:%06lX D:%02lu/%02lu",
             (unsigned long)off, (unsigned long)done, (unsigned long)total);
    LCDColorSet(PF_COLOR_INFO);
    pf_lcd_line(1, l1);
    pf_lcd_line(2, l2);
}






/* ---- 내부 상태(정적) ---- */
static uint8_t  g_cfg_inited = 0;
static uint32_t g_cfg_next_seq = 1;
static uint32_t g_cfg_write_ptr = CFG_FLASH_BASE_ADDR;
static FlashCfgPayload g_shadow;  // 마지막 저장본(비교용)

/* ---- CRC32 (poly 0xEDB88320, bitwise – 작은 데이터용) ---- */
static uint32_t cfg_crc32(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i=0;i<len;++i) {
        crc ^= p[i];
        for (uint8_t b=0;b<8;++b) {
            uint32_t m = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320UL & m);
        }
    }
    return ~crc;
}

/* ---- 유틸: 32비트 읽기/쓰기 ---- */




static HAL_StatusTypeDef wr32(uint32_t addr, uint32_t word) {
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
}

/* ---- 유틸: 다음 공백(0xFF..) 위치 찾기 ---- */
static uint32_t cfg_find_write_ptr(void) {
    uint32_t a = CFG_FLASH_BASE_ADDR;
    while (a + sizeof(FlashCfgHeader) < CFG_FLASH_END_ADDR) {
        FlashCfgHeader h;
        memcpy(&h, (void*)a, sizeof(FlashCfgHeader));

        if (h.magic == 0xFFFFFFFFUL) break;                // blank → 여기부터 쓰기
        if (h.magic != CFG_MAGIC) break;                   // 다른 데이터 → 여기부터 쓰기
        if (h.payload_size == 0 || h.payload_size > 1024) break;

        uint32_t ps = h.payload_size;
        uint32_t rec_size = sizeof(FlashCfgHeader) + ((ps + 3u) & ~3u);

        // ★ CRC로 payload 완전성 검증 (부분기록이면 STOP)
        uint32_t crc = cfg_crc32((void*)(a + sizeof(FlashCfgHeader)), ps);
        if (crc != h.crc32) break;                         // 덜 써진 레코드 → 여기부터 쓰기

        a += rec_size;                                     // 정상 레코드 → 다음 후보
    }
    return a;
}


/* ---- 유틸: 최신 유효 레코드 스캔 & 로드 ---- */
// PowerFlash.c 안에 넣기: 기존 cfg_scan_and_load_last() 완전 교체

// 호환할 이전 버전들
/* ---- 유틸: 최신 유효 레코드 스캔 & 로드 (섹터 9~11 링 스캔) ---- */
static int cfg_scan_and_load_last(void) {
    uint32_t best_seq = 0;
    uint32_t best_rec_end = CFG_FLASH_BASE_ADDR;   // 마지막 유효 레코드의 다음 주소
    FlashCfgPayload best_payload;
    int found = 0;

    // 섹터 9 → 10 → 11 → (끝)
    uint32_t sec_base = CFG_FLASH_BASE_ADDR;
    for (int sec_iter = 0; sec_iter < 3; ++sec_iter) {
        uint32_t pos = sec_base;
        uint32_t sec_end = pf_sector_end_of(sec_base);

        while (pos + sizeof(FlashCfgHeader) <= sec_end) {
            FlashCfgHeader h;
            memcpy(&h, (void*)pos, sizeof(FlashCfgHeader));

            // 블랭크(0xFFFFFFFF..)면 이 섹터는 더 볼 것 없음 → 다음 섹터
            if (h.magic == 0xFFFFFFFFu) break;

            // 다른 데이터(매직 미일치)면 보수적으로 섹터 넘김
            if (h.magic != CFG_MAGIC) break;

            uint32_t ps = h.payload_size;
            if (ps == 0 || ps > 1024) break;

            uint32_t rec_size = sizeof(FlashCfgHeader) + ((ps + 3u) & ~3u);

            // 버전 호환 확인
            int supported = (h.version == CFG_VERSION_CUR) ||
                            (h.version == CFG_VERSION_OLD1) ||
                            (h.version == CFG_VERSION_OLD0);

            if (supported) {
                uint32_t crc = cfg_crc32((void*)(pos + sizeof(FlashCfgHeader)), ps);
                if (crc == h.crc32) {
                    if (h.seq >= best_seq) {
                        // payload 복사 (짧은 구버전이면 남는 바이트는 0)
                        FlashCfgPayload tmp;
                        memset(&tmp, 0, sizeof(tmp));
                        memcpy(&tmp,
                               (void*)(pos + sizeof(FlashCfgHeader)),
                               ps < sizeof(FlashCfgPayload) ? ps : sizeof(FlashCfgPayload));

                        // 구버전 보정(현행에 새로 추가된 필드 기본값 채우기)
                        if (h.version != CFG_VERSION_CUR) {
                            // 과거 리비전에서 넣었던 기본값 유지
                            tmp.MetronomeBPM  = 130;
                            tmp.TimeSignature = 4;
                            tmp.TunerBaseFreq = 440;
                            tmp.SoundGenMode  = 0;

                            // 새 센서/튜너 보정값 기본
                            tmp.g_tnr_sens            = 0;
                            tmp.TempUnitF             = 0;               // 0=C
                            tmp.Cal_RH_Offset_u32     = f2u(0.0f);
                            tmp.Cal_RH_Scale_u32      = f2u(1.0f);
                            tmp.Cal_TempC_Offset_u32  = f2u(0.0f);
                            tmp.Cal_TempC_Scale_u32   = f2u(1.0f);
                        }

                        best_payload = tmp;
                        best_seq     = h.seq;
                        best_rec_end = pos + rec_size;
                        found        = 1;
                    }
                }
            }

            // 같은 섹터 내 다음 레코드
            pos += rec_size;
        }

        // 다음 섹터(링)
        sec_base = pf_next_sector_base(sec_base);
    }

    if (found) {
        // ---- 전역 반영 ----
        MasterVolume           = best_payload.MasterVolume;
        SoundBalance           = best_payload.SoundBalance;
        SFXVolume              = best_payload.SFXVolume;
        CurrentNoteIndex       = best_payload.CurrentNoteIndex;
        PitchSemitone          = best_payload.PitchSemitone;
        MicBoost_dB            = best_payload.MicBoost_dB;
        MicAGC_On              = best_payload.MicAGC_On;
        MicInputMode           = best_payload.MicInputMode;
        AutoVU_After10s        = best_payload.AutoVU_After10s;
        TunerCalibrationValue  = best_payload.TunerCalibrationValue;

        CutOffFreqStart        = best_payload.CutOffFreqStart;
        CutOffFreqEnd          = best_payload.CutOffFreqEnd;
        CutOffFreqStartUser1   = best_payload.CutOffFreqStartUser1;
        CutOffFreqEndUser1     = best_payload.CutOffFreqEndUser1;
        CutOffFreqStartUser2   = best_payload.CutOffFreqStartUser2;
        CutOffFreqEndUser2     = best_payload.CutOffFreqEndUser2;
        CutOffFreqStartUser3   = best_payload.CutOffFreqStartUser3;
        CutOffFreqEndUser3     = best_payload.CutOffFreqEndUser3;

        MetronomeBPM           = best_payload.MetronomeBPM;
        TimeSignature          = best_payload.TimeSignature;
        TunerBaseFreq          = best_payload.TunerBaseFreq;
        SoundGenMode           = best_payload.SoundGenMode;

        g_sw_total_ms          = best_payload.g_sw_total_ms;
        g_timer_set_min        = best_payload.g_timer_set_min;
        g_timer_remaining_ms   = best_payload.g_timer_remaining_ms;
        g_sw_now_ms            = best_payload.g_sw_now_ms;
        g_pract_menu_index     = best_payload.g_pract_menu_index;
        CutOffOnOff            = best_payload.CutOffOnOff;
        CurrentInstrumentType  = (int)best_payload.CurrentInstrumentType;
        g_timer_run            = best_payload.g_timer_run;

        // 센서/튜너 보정
        g_tnr_sens             = best_payload.g_tnr_sens;
        TempUnitF              = best_payload.TempUnitF;
        Cal_RH_Offset          = u2f(best_payload.Cal_RH_Offset_u32);
        Cal_RH_Scale           = u2f(best_payload.Cal_RH_Scale_u32);
        Cal_TempC_Offset       = u2f(best_payload.Cal_TempC_Offset_u32);
        Cal_TempC_Scale        = u2f(best_payload.Cal_TempC_Scale_u32);

        // ---- 그림자/시퀀스/다음 쓰기 포인터 ----
        FlashCfgPayload norm;
        cfg_build_snapshot(&norm);
        g_shadow       = norm;
        g_cfg_next_seq = best_seq + 1;

        // 다음 기록 위치: 마지막 레코드 끝
        g_cfg_write_ptr = best_rec_end;
        // 섹터 말미에 헤더 한 장 못 들어가면 다음 섹터로 이동
        if (g_cfg_write_ptr + sizeof(FlashCfgHeader) > pf_sector_end_of(g_cfg_write_ptr)) {
            g_cfg_write_ptr = pf_next_sector_base(g_cfg_write_ptr);
        }
    } else {
        // 아무것도 없으면 현재 전역값을 그림자로(정규화) + 첫 섹터 시작
        FlashCfgPayload cur;
        cfg_build_snapshot(&cur);
        g_shadow        = cur;
        g_cfg_next_seq  = 1;
        g_cfg_write_ptr = CFG_FLASH_BASE_ADDR;
    }

    return found;
}




/* ---- 유틸: 섹터 지우기 ---- */
static HAL_StatusTypeDef cfg_erase_sector_at(uint32_t any_addr_in_sector) {
    HAL_StatusTypeDef st;
    FLASH_EraseInitTypeDef ei;
    uint32_t se = pf_sector_index_of(any_addr_in_sector);
    uint32_t pe;

    __disable_irq();
    HAL_FLASH_Unlock();

    ei.TypeErase    = FLASH_TYPEERASE_SECTORS;
    ei.Sector       = se;
    ei.NbSectors    = 1;
    ei.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 2.7~3.6V (보통 F407 보드)
    st = HAL_FLASHEx_Erase(&ei, &pe);

    HAL_FLASH_Lock();
    __enable_irq();
    return st;
}


/* ---- 유틸: 레코드 쓰기(append) ---- */
static HAL_StatusTypeDef cfg_write_record(const FlashCfgPayload *pl) {
    FlashCfgHeader h;
    h.magic        = CFG_MAGIC;
    h.version      = CFG_VERSION;
    h.seq          = g_cfg_next_seq++;
    h.payload_size = (uint32_t)sizeof(FlashCfgPayload);
    h.crc32        = cfg_crc32(pl, sizeof(FlashCfgPayload));

    uint32_t rec_bytes = sizeof(FlashCfgHeader) + ((sizeof(FlashCfgPayload)+3u)&~3u);

    // (A) 현재 섹터의 남은 공간 부족하면 → 다음 섹터 베이스로 이동
    uint32_t cur_sec_end = pf_sector_end_of(g_cfg_write_ptr);
    if (g_cfg_write_ptr + rec_bytes > cur_sec_end) {
        uint32_t nb = pf_next_sector_base(g_cfg_write_ptr);
        if (FlashDebug) pf_lcd_verbose_phase("ADV SECTOR", nb, 0, 0);
        g_cfg_write_ptr = nb;
    }

    // (B) 대상 범위가 blank가 아니면(=찌꺼기 존재) → 해당 섹터 지우고 깨끗이 시작
    if (!cfg_is_blank(g_cfg_write_ptr, rec_bytes)) {
        if (FlashDebug) pf_lcd_verbose_phase("ERASE RECOV", g_cfg_write_ptr, 0, 0);
        if (cfg_erase_sector_at(g_cfg_write_ptr) != HAL_OK) {
            if (FlashDebug) pf_lcd_verbose_phase("SAVE FAIL", g_cfg_write_ptr, 0, 0);
            pf_lcd_simple_color("ERASE ERR!", "RECOVERY     ", PF_COLOR_ERR);
            return HAL_ERROR;
        }
    }


    // 1) HEADER 안내 (VERBOSE만)
    if (FlashDebug) pf_lcd_verbose_phase("SAVE HDR", g_cfg_write_ptr, 0, (uint32_t)(sizeof(FlashCfgHeader)/4));

    HAL_StatusTypeDef st = HAL_OK;
    __disable_irq();
    HAL_FLASH_Unlock();
    const uint32_t *ph = (const uint32_t*)&h;
    for (uint32_t off=0; off<sizeof(FlashCfgHeader); off+=4) {
        st = wr32(g_cfg_write_ptr + off, ph[off/4]);
        if (st != HAL_OK) break;
    }
    HAL_FLASH_Lock();
    __enable_irq();

    /* 2) PAYLOAD */
    if (st == HAL_OK) {
        uint32_t pay_addr = g_cfg_write_ptr + sizeof(FlashCfgHeader);
        uint32_t padded   = ((uint32_t)sizeof(FlashCfgPayload)+3u)&~3u;
        uint32_t words    = padded / 4u;

        if (FlashDebug) pf_lcd_verbose_phase("SAVE PAY", pay_addr, 0, words);

        uint32_t tmpbuf_words[(1024/4)];
        memset(tmpbuf_words, 0xFF, sizeof(tmpbuf_words));
        memcpy(tmpbuf_words, pl, sizeof(FlashCfgPayload));

        HAL_FLASH_Unlock();

        const uint32_t STEP = 1; // 진행표시 한 칸마다 갱신하려면 1, 더 빠르게 하려면 8 등
        HAL_StatusTypeDef st_local = HAL_OK;

        for (uint32_t i = 0; i < words; ++i) {
            // 진행 표시 (IRQ ON에서만)
            if (FlashDebug && ((i % STEP) == 0)) {
                pf_lcd_verbose_phase("SAVE PAYLD", (pay_addr + 4*i), (i), words);
                pf_delay_ms(PF_DELAY_STEP_MS);  // ★ 스텝마다 250ms(기본)
            }
            // 실제 쓰기 (아주 짧게 IRQ OFF)
            __disable_irq();
            st_local = wr32(pay_addr + 4*i, tmpbuf_words[i]);
            __enable_irq();
            if (st_local != HAL_OK) { st = st_local; break; }
        }

        // ★ 잠금 복구 빠져있던 부분 보강!
        HAL_FLASH_Lock();
    } // ★★★←←← 이 '}' 가 빠져 있었음! (중요)★★★

    if (st == HAL_OK) {
        g_cfg_write_ptr += rec_bytes;
        if (FlashDebug) pf_lcd_verbose_phase("SAVED", g_cfg_write_ptr, 1, 1);
        pf_lcd_simple_color("                ", "SAVE SUCCESS    ", PF_COLOR_OK);
        pf_delay_ms(PF_DELAY_SAVED_MS);  // ★ 1초 유지
        pf_delay_ms(FlashDebug ? PF_DELAY_SAVED_MS : PF_DELAY_QUIET_MS);   // ★ 교체
    } else {
        if (FlashDebug) pf_lcd_verbose_phase("SAVE FAIL", g_cfg_write_ptr, 0, 0);
        pf_lcd_simple_color("                ", "SAVE ERROR!     ", PF_COLOR_ERR);
        pf_delay_ms(PF_DELAY_SAVED_MS);  // ★ 1초 유지
        pf_delay_ms(FlashDebug ? PF_DELAY_SAVED_MS : PF_DELAY_QUIET_MS);   // ★ 교체
    }
    return st;
}


/* ========================== PUBLIC: 단일 서비스 함수 ==========================
 * 사용법:
 *  - 부팅 직후 while 전에   : ConfigStorage_Service(0);  // 로드
 *  - 값 저장 트리거가 생길 때: ConfigStorage_Service(1);  // 변경 시에만 append 기록
 * ============================================================================
 */


void ConfigStorage_Service(uint8_t trigger_save)
{
    // ── 최초 1회: 스캔 & 로드 ──
	// ── 최초 1회: 스캔 & 로드 ──
	if (!g_cfg_inited) {
	    // ★ 디버그 배너가 보이도록, 배너 직후 살짝 대기 추가
	    if (FlashDebug) {
	        pf_lcd_verbose_phase("LOAD BEGIN", CFG_FLASH_BASE_ADDR, 0, 0);
	        pf_delay_ms(PF_DELAY_STEP_MS);            // 예: 200ms
	    }

	    int ok = cfg_scan_and_load_last();
	    g_cfg_inited = 1;

	    if (ok) {
	        if (FlashDebug) {
	            pf_lcd_verbose_phase("LOAD OK", g_cfg_write_ptr, 1, 1);
	            pf_delay_ms(PF_DELAY_STEP_MS);        // 예: 200ms
	        }
	        pf_lcd_simple_color("                ", "LOAD SUCCESS    ", PF_COLOR_OK);
	        pf_delay_ms(FlashDebug ? 500U : PF_DELAY_QUIET_MS);  // 디버그ON=0.5s, OFF=0.5s
	    } else {
	        if (FlashDebug) {
	            pf_lcd_verbose_phase("LOAD DEF", CFG_FLASH_BASE_ADDR, 0, 0);
	            pf_delay_ms(PF_DELAY_STEP_MS);        // 예: 200ms
	        }
	        // 네가 현재 쓰는 문구/시간 유지 (원하면 DEFAULT/NO RECORD로 바꿔도 OK)
	        pf_lcd_simple_color("SAVE DAMAGED ", " NO RECORD! ", PF_COLOR_INFO);
	        pf_delay_ms(FlashDebug ? 3000U : 3000U);
	    }
	    return;
	}

    if (!trigger_save) return;

    // ── 스냅샷 생성(패딩 0) ──
    FlashCfgPayload cur;
    cfg_build_snapshot(&cur);
    // ── 변경 없으면 저장 스킵 ──
    if (memcmp(&cur, &g_shadow, sizeof(FlashCfgPayload)) == 0) {
        if (FlashDebug) {
            pf_lcd_simple_color("SKIP FLASH SAVE", "NO CHANGES MADE ", PF_COLOR_INFO);
            pf_delay_ms(PF_DELAY_SAVED_MS);   // 요청대로: 디버그때만 표시/대기
        }

        ModeSelectionFirstRunFlag = PracticeHomeFirstRunFlag = TunerHomeFirstRunFlag =
        MetronomeHomeFirstRunFlag = SoundGenHomeFirstRunFlag = SettingsHomeFirstRunFlag =
        VolumeControlFirstRunFlag = BalanceControlFirstRunFlag = 0;

        return;


    }

    // ── 기록 ──
    if (cfg_write_record(&cur) == HAL_OK) {
        g_shadow = cur; // 그림자 갱신

        // 저장 성공 시 FirstRun 플래그 초기화 (너가 원하던 한 줄)
        ModeSelectionFirstRunFlag = PracticeHomeFirstRunFlag = TunerHomeFirstRunFlag =
        MetronomeHomeFirstRunFlag = SoundGenHomeFirstRunFlag = SettingsHomeFirstRunFlag =
        VolumeControlFirstRunFlag = BalanceControlFirstRunFlag = 0;
    } else {
        // 실패 시: 토스트/색상/딜레이는 cfg_write_record 내부에서 이미 처리됨
        ModeSelectionFirstRunFlag = PracticeHomeFirstRunFlag = TunerHomeFirstRunFlag =
        MetronomeHomeFirstRunFlag = SoundGenHomeFirstRunFlag = SettingsHomeFirstRunFlag =
        VolumeControlFirstRunFlag = BalanceControlFirstRunFlag = 0;
    }
}



