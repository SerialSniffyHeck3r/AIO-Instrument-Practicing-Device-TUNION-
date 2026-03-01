/* ============================================================================
 * LEDcontrol.c — L2R FLOW (STM32F407)
 *
 * 목표
 *  - 저휘도 계단/플리커 제거: Pre-Gamma(선형) 스무딩 + 감마 보간 + 전 경로 1 kHz
 *  - 기존 API/동작 보존: LED_SetFlowSpeed/Spread/ModeSet()
 *  - 논블로킹, ISR 정수(Q포맷)+LUT (float은 초기화/설정자 한정)
 *  - 타이밍: TIM6=1 kHz(핵심+ΣΔ), TIM7=120 Hz(호환용 no-op)
 *
 * 파이프라인
 *   [전역/상수] → [효과 LUT] → [상태(Q)] → [분배(4탭, Q12 보간)]
 *      → [Pre-Gamma 스무딩(Q20)] → [감마+보간 → PWM(Q16)]
 *      → [ΣΔ → CCR] → [Init/ISR]
 *
 * 섹션 목차
 *   0) 전역 상수/매핑, 외부 핸들
 *   1) 효과 LUT (Gamma/Hann/k(dt), Ease)
 *   2) 상태·버퍼·유틸 (Q 포맷/타이머 유틸 등)
 *   3) 분배 헬퍼(fill_targets_lin_q20_from_pos)
 *   4) 패턴 (Pattern_SweepL2R) & 디스패처(LED_PatternDispatch)
 *   5) 렌더 코어: LED_RenderOneTick (한 틱 전체 수행) + LED_Core_1kHz(래퍼)
 *   6) 모드/ISR (LED_ModeSet, LED_SD_1kHz_ISR 등)
 *   7) 퍼블릭 설정자 (속도/스프레드/스무딩 τ)
 *   8) PWM 초기화 + LED_Init
 *
 * 확장 가이드
 *   - 새 패턴: Pattern_XXX() 추가 → LED_PatternDispatch() case에 매핑
 *   - UI 연동: main.c의 RenderUI()에서 LEDModeSet(번호) 한 줄로 제어
 * ========================================================================== */





#include "main.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* forward declarations (패턴/디스패처) */
static inline void Pattern_SweepL2R(uint32_t pos_q88);
void LED_PatternDispatch(uint32_t pos_q88);
static inline void LED_Bar_FromMasterVolume(void);
static inline void LED_Bar_FromSoundBalance(void);
static inline void LED_RenderOneTick(void);
static inline void Pattern_Breath_Center(void);
static inline void LED_Bar_FromNoteIndex(void);
static inline void Pattern_SweepL2R(uint32_t pos_q88);
void LED_PatternDispatch(uint32_t pos_q88);
static inline void LED_Bar_FromMasterVolume(void);
static inline void LED_Bar_FromSoundBalance(void);
static inline void LED_Bar_FromMetronomeVolume(void);   // [ADD]
static inline void LED_RenderOneTick(void);
static inline void Pattern_Breath_Center(void);
static inline void LED_Bar_FromNoteIndex(void);




/* --- 외부 타이머 핸들 (보드 매핑) ----------------------------------------- */
extern TIM_HandleTypeDef htim3;   // TIM3: CH3, CH4  (APB1=84MHz tmrclk)
extern TIM_HandleTypeDef htim5;   // TIM5: CH1, CH3, CH4 (APB1)
extern TIM_HandleTypeDef htim9;   // TIM9: CH1, CH2  (APB2=168MHz tmrclk)
extern TIM_HandleTypeDef htim12;  // TIM12: CH1, CH2 (APB1)

/* 밖에서 오는 것*/
extern volatile uint8_t g_led_brightness_level;

extern volatile uint16_t MasterVolume;   // 0..50
extern volatile uint16_t SoundBalance;   // 0..50

/* === Metronome globals from main.c (read-only) ========================== */
extern volatile uint16_t MetronomeBPM;   /* 30..300 */
extern volatile uint8_t  TimeSignature;  /* 1..9    */
extern volatile uint8_t  IsMetronomeReady;  /* 0/1  */
extern volatile uint8_t  TimeSignatureDen;  /* 1..32  새로 추가 */

/* [NEW] Shared metronome phase (from main.c, Option A) */
extern volatile uint32_t g_met_phi_q16;     // 0..65535
extern volatile uint8_t  g_met_beat_inbar;  // 0..(TimeSignature-1)
extern volatile uint8_t  g_met_ready;       // 0/1

extern volatile uint16_t MetronomeVolume;  // [ADD] 0..50

/* === SoundGen 전역 ================================================ */
/* 현재 음 인덱스: A0=0, A7=7 (main.c에서 관리) */
extern volatile uint8_t CurrentNoteIndex;   /* 0..7 만 사용 */
/* 준비 상태: 1=발진 중(깜빡임 적용), 0=대기(깜빡임 없음) */
extern volatile uint8_t IsSoundGenReady;    /* 0/1 */

/* === [TUNER externs] UI/DSP 공유 변수 === */
extern volatile float    TunerMeasurement;   // 0 이면 미동작(무음/비활성)로 취급
extern volatile uint16_t CurrnetTunerNote;   // 현재 근접 음의 인덱스(참조만)
extern volatile uint16_t CurrentTunerCent;   // -50..+50 (signed로 캐스팅해 사용)

/* === [TUNER 표시 파라미터] === */
volatile uint8_t g_tnr_center_idle_pct = 30;   // 튜너 미동작 시 가운데 LED 최소 밝기(%)
/* [ADD] 좌(I2S2)·우(I2S3) VU 세그먼트(0..30) — 프로젝트 변수명에 맞춰 수정하세요.
   - 내부에서 seg30>=29를 '−3 dBFS 초과'로 봅니다. (−60..0dB → 0..30 선형 매핑 가정) */

// [ADD] VU 세그먼트 가져오는 공식 API
extern void notch_get_vu_segments(uint8_t *outSegL, uint8_t *outSegR); // I2S2 L/R
extern void notch_get_mic_vu_segments(uint8_t *seg_out);               // I2S3 mono

//extern volatile uint16_t s_vu_track30;  // I2S2 쪽 seg30 (예시 변수명)
//extern volatile uint16_t s_vu_mic30;    // I2S3 쪽 seg30 (예시 변수명)

// [ADD] SoundGen 현재 음 인덱스 (main.c에 존재)
extern volatile uint8_t CurrentNoteIndex;  // 0..(NOTE_COUNT-1)

/* ===== 0) 전역 상수/매핑 & 외부 핸들 ===================================== */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NOTE_COUNT 85

/* [옵션] NoteNames 길이를 모듈에서 못 받는다면, 아래 매크로를 파일 상단 어딘가에 둬.
   네 배열(A0..A7)은 총 85개(7옥타브*12 + 1)라서 85가 맞음. 이미 다른 곳에서 정의돼 있으면 이 블록은 무시됨. */
#ifndef SG_NOTE_COUNT
#define SG_NOTE_COUNT 85u
#endif

/* --- Breathing timing (ms) ----------------------------------------------- */
/* 안정 호흡 느낌: 들이쉼 2.8s, 고점홀드 0.7s, 내쉼 3.2s, 저점휴식 0.9s */
#define BREATH_IN_MS       4100u   /* 들이쉼 */
#define BREATH_HOLD_MS      700u   /* 고점에서 잠깐 멈춤 */
#define BREATH_OUT_MS      3800u   /* 내쉼 */
#define BREATH_REST_MS     2000u   /* 저점에서 쉬어가기 */
#define BREATH_TOTAL_MS    (BREATH_IN_MS + BREATH_HOLD_MS + BREATH_OUT_MS + BREATH_REST_MS)

/* 1kHz 한 틱당 +1ms 누적 (ISR 주기가 1ms라 가정) */
static uint32_t s_breath_t_ms = 0;


/* === Pattern params (compile-time knobs) ================================ */
#ifndef MET_BAND_LEDS          /* 한 번에 켜질 LED 개수(2 또는 3 권장) */
#define MET_BAND_LEDS 3u
#endif

#ifndef MET_GROW_FRAC_Q8       /* (0..255) 비트 단위: 성장 구간 비율 (≈0.25) */
#define MET_GROW_FRAC_Q8 64u
#endif
#ifndef MET_SHRINK_FRAC_Q8     /* (0..255) 비트 단위: 수축 구간 비율 (≈0.25) */
#define MET_SHRINK_FRAC_Q8 64u
#endif

#ifndef MET_END_BASE_PERMILLE  /* 끝(1번/9번) LED의 항상-켜짐 기준(퍼밀) */
#define MET_END_BASE_PERMILLE 500u   /* ≈ 50% */
#endif

#ifndef MET_LEN_SLOW_BPM
#define MET_LEN_SLOW_BPM   60u   /* 느리면 +1칸 */
#endif
#ifndef MET_LEN_FAST_BPM
#define MET_LEN_FAST_BPM  140u   /* 빠르면 -1칸 */
#endif
#ifndef MET_LEN_DELTA_MAX
#define MET_LEN_DELTA_MAX   1u   /* 길이 가변 최대폭(칸) */
#endif


// [ADD] LED 모드 번호: 메트로놈 볼륨 바
#ifndef LED_MODE_METRONOME_VOLUME
#define LED_MODE_METRONOME_VOLUME 7u
#endif




/* PWM: 1kHz, 12-bit+ 해상도(ARR=41999 → 약 15.4bit) */
#define PWM_ARR          41999u
#define PWM_MAX          PWM_ARR
#define PWM_HZ           1000u

/* 위치 표현: Q8.8 (1 LED칸 = 256) */
#define QPOS_SHIFT       8

/* 선형밝기 범위(감마 LUT 입력): 0..4096 (Q12) */
#define LIN_MAX          4096u

/* 선형 스무딩 내부 포맷: Q12.8 (정수=0..4096, 분수=8bit) */
#define LIN_FRAC_BITS    8u
#define LIN_Q            (LIN_FRAC_BITS)


// ==== Clip Warn (지속 과레벨만 경고) =========================================
#ifndef LED_MODE_CLIP_WARN
#define LED_MODE_CLIP_WARN  8u
#endif

#define CLIP_SEG_TH              29u   // seg30≥29 ≈ -3 dBFS 초과 (임계 그대로)
#define CLIP_ON_TIME_MS         300u   // 이 시간 이상 연속 초과되어야 'ON'
#define CLIP_OFF_TIME_MS        120u   // 이 시간 이하로 내려오면 'OFF'
#define CLIP_DECAY_PER_FRAME_MS  32u   // 임계 미만일 때 매 프레임 감소(ms)
#define CLIP_BLINK_PERIOD_MS    800u   // 점멸 주기
#define CLIP_BLINK_ON_MS        220u   // 켜짐 구간

#ifndef LED_FRAME_MS
#define LED_FRAME_MS             16u   // 한 프레임 시간(대략)
#endif

static uint16_t s_over_ms_L = 0, s_over_ms_R = 0; // 임계 이상 누적 시간
static uint16_t s_blink_tmr_L = 0, s_blink_tmr_R = 0;
static uint8_t  s_active_L   = 0, s_active_R   = 0; // 현재 경고 상태



/* 강한 ease-in-ou
 *
 * t(Quintic) 0..255 → 0..255 */
static uint8_t s_ease256[256];

/* 퍼블릭 토글(0=OFF, 1=ON) — 외부에서 직접 바꿔 쓰기 */
volatile uint8_t g_led_ease_sweep_on = 1;  // 전체 스윕 곡선
volatile uint8_t g_led_ease_local_on = 0;  // 칸 내부 분수 곡선

volatile uint8_t g_led_mode_sel = 1;

/* === Metronome-synced band state ======================================= */
static uint8_t  s_met_inited         = 0;
static uint32_t s_met_beat_ms        = 500;   /* 120BPM 가정 초기값 */
static uint32_t s_met_beat_start_ms  = 0;
static uint8_t  s_met_beat_in_bar    = 0;     /* 0..(TimeSignature-1) */
static uint16_t s_prev_bpm           = 0;
static uint8_t  s_prev_ts            = 0;


enum { NLED = 9 };


/* LED 핀→타이머 채널 매핑 (좌→우) */
typedef struct { TIM_HandleTypeDef* h; uint32_t ch; } LedOut;
static LedOut s_led[NLED] = {
    { &htim3,  TIM_CHANNEL_3 }, // 0
    { &htim3,  TIM_CHANNEL_4 }, // 1
    { &htim5,  TIM_CHANNEL_1 }, // 2
    { &htim5,  TIM_CHANNEL_3 }, // 3
    { &htim5,  TIM_CHANNEL_4 }, // 4
    { &htim9,  TIM_CHANNEL_1 }, // 5
    { &htim9,  TIM_CHANNEL_2 }, // 6
    { &htim12, TIM_CHANNEL_1 }, // 7
    { &htim12, TIM_CHANNEL_2 }, // 8
};

/* 전역 밝기 스케일(퍼밀, 감마 이후에 적용) */
static const uint16_t s_level_scale_permille[3] = { 300, 600, 1000 };



/* ===== 1) 효과 LUT (Gamma/Hann/Ease/k(dt)) ================================ */
/* γ=2.2 : 선형 0..LIN_MAX → PWM 0..PWM_MAX (정수) */
static uint16_t s_gamma[LIN_MAX + 1];
/* Hann(raised cosine) 0..255 → Q15(0..32767) (분배용) */
static uint16_t s_hann256[256];
/* dt(ms)→ k(dt)=1-exp(-dt/τ)  (Q15) */
static uint16_t s_k_dt_q15[65];   // dt=0..64ms

static void BuildGammaLUT(void){
    for (uint32_t i=0; i<=LIN_MAX; ++i){
        float x = (float)i / (float)LIN_MAX;      // 0..1
        float y = powf(x, 2.2f);                  // γ
        uint32_t v = (uint32_t)lroundf(y * (float)PWM_MAX);
        if (v > PWM_MAX) v = PWM_MAX;
        s_gamma[i] = (uint16_t)v;
    }
}
static void BuildHannLUT(void){
    for (int i=0; i<256; ++i){
        float t = (float)i / 255.f;                        // 0..1
        float h = 0.5f * (1.f + cosf((float)M_PI * t));    // 1..0
        int v = (int)lroundf(h * 32767.f);                 // Q15
        if (v < 0) v = 0; if (v > 32767) v = 32767;
        s_hann256[i] = (uint16_t)v;
    }
}
static void BuildSmoothingTable(uint16_t tau_ms){
    if (tau_ms < 1) tau_ms = 1;
    for (uint32_t dt=0; dt<=64; ++dt){
        float k = 1.f - expf(-(float)dt / (float)tau_ms);    // 0..1
        uint32_t q = (uint32_t)lroundf(k * 32767.f);
        if (q > 32767u) q = 32767u;
        s_k_dt_q15[dt] = (uint16_t)q;                        // Q15
    }
}



/* 강한 S-커브: easeInOutQuint (초/후반 매우 느림, 중간 급가속) */


static void BuildEaseLUT_Strong(void){
    for (int i=0; i<256; ++i){
        float t = (float)i / 255.f;
        float y;
        if (t < 0.5f){
            float u = 2.f * t;
            y = 0.5f * u*u*u*u*u;               // 0.5*(2t)^5
        }else{
            float u = 2.f * (1.f - t);
            y = 1.f - 0.5f * u*u*u*u*u;         // 1 - 0.5*(2(1-t))^5
        }
        int v = (int)lroundf(y * 255.f);
        if (v < 0) v = 0; if (v > 255) v = 255;
        s_ease256[i] = (uint8_t)v;
    }
}

/* ===== 2) 헬퍼/상태 ======================================================== */
/* 속도: 초당 Q8.8 (FlowSpeed=1 → 1 LED/s → 256 / s) */
static uint32_t s_step_per_sec_q88 = (1u << QPOS_SHIFT);

/* 퍼짐(LED칸) */
static uint16_t s_spread_q8 = (uint16_t)(1.7f * 256.f + 0.5f);

/* 스무딩 시정수 τ(ms) */
static uint16_t s_tau_ms = 30;

/* 모드/위치/시간 */
static uint8_t  s_mode    = 1;         // 0=Off, 1=Flow
static uint32_t s_pos_q88 = 0;
static uint32_t s_last_ms = 0;         // HAL_GetTick()

/* 하드웨어 상태/ΣΔ 누산기 */
static uint16_t s_pwm_out[NLED];       // 마지막 CCR
static uint32_t s_sd_acc[NLED];        // ΣΔ 누산기(0..65535)

/* === Pre-Gamma 선형 스무딩 상태/타깃 (Q12.8) =============================== */
static uint32_t s_lin_out_q20[NLED];   // 스무딩된 선형 밝기 (0..LIN_MAX<<8)
static uint32_t s_lin_tgt_q20[NLED];   // 타깃 선형 밝기 (분배 결과)

/* PWM 도메인 출력(Q16) — ΣΔ 앞단 */
static uint32_t s_out_q16[NLED];

/* TIM/PWM 유틸 */
static inline void tim_apply_pwm(TIM_HandleTypeDef* h, uint16_t psc, uint16_t arr){
    __HAL_TIM_DISABLE(h);
    __HAL_TIM_SET_PRESCALER(h, psc);
    __HAL_TIM_SET_AUTORELOAD(h, arr);
    __HAL_TIM_SET_COUNTER(h, 0);
    h->Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_ENABLE(h);
}
static inline void pwm_start(TIM_HandleTypeDef* h, uint32_t ch){
    __HAL_TIM_SET_COMPARE(h, ch, 0);
    HAL_TIM_PWM_Start(h, ch);
}
static inline void led_set(uint8_t idx, uint16_t duty){
    if (idx >= NLED) return;
    if (duty > PWM_MAX) duty = PWM_MAX;
    __HAL_TIM_SET_COMPARE(s_led[idx].h, s_led[idx].ch, duty);
}


/* === 단일 렌더러: 한 틱에 할 일을 모두 수행 ========================= */
/* - 앞으로 UI/튜너/메트로놈/볼륨 등의 전역 상태를 읽어
 *   '어떤 패턴을 쓸지' 결정하는 로직을 이 함수에만 넣으면 됨. */
static inline void LED_RenderOneTick(void)
{
    /* 1) 시간/Δt(ms) */
    uint32_t now = HAL_GetTick();
    uint32_t dt  = now - s_last_ms;
    if (dt == 0) dt = 1;
    s_last_ms = now;

    /* 2) 위치 전진(Q8.8) + 래핑 */
    uint32_t adv = (s_step_per_sec_q88 * dt + 500u) / 1000u;
    s_pos_q88 += adv;
    uint32_t limit_q = (NLED << QPOS_SHIFT);
    if (s_pos_q88 >= limit_q) s_pos_q88 %= limit_q;  // :contentReference[oaicite:13]{index=13}

    /* === (여기) 전역 상태를 보고 모드/패턴 결정 가능 ====================== */
    /* 예: 튜너/메트로놈/볼륨 상태를 읽어 g_led_mode_sel 바꾸거나
           추가적인 보조 지표(예: BPM 박자 위상)를 만들어
           Pattern_XXX에서 활용하도록 넘길 수 있음.
           (지금은 모드 전환만: g_led_mode_sel 을 그대로 사용) */

    /* 3) 분배 → 선형 타깃(Q12.8)  [기존 fill_targets... 대신 디스패처] */
    LED_PatternDispatch(s_pos_q88);

    /* 4) Pre-Gamma 스무딩(Q12.8) */
    uint32_t dt_clamp = (dt > 64u) ? 64u : dt;
    uint16_t kq15 = s_k_dt_q15[dt_clamp];
    for (int k=0; k<NLED; ++k){
        uint32_t tgt = s_lin_tgt_q20[k];
        uint32_t out = s_lin_out_q20[k];
        int32_t  err = (int32_t)tgt - (int32_t)out;
        int32_t  delta = ( (int64_t)err * (int64_t)kq15 + 16384 ) >> 15;
        int32_t  newv = (int32_t)out + delta;
        if (newv < 0) newv = 0;
        if ((uint32_t)newv > (LIN_MAX<<LIN_Q)) newv = (int32_t)(LIN_MAX<<LIN_Q);
        s_lin_out_q20[k] = (uint32_t)newv;
    }

    /* 5) 감마 LUT + 8bit 보간 → PWM → 전역밝기 스케일 → Q16 */
    uint8_t  lvl = (g_led_brightness_level > 2) ? 2 : g_led_brightness_level;
    uint32_t permille = s_level_scale_permille[lvl];
    for (int k=0; k<NLED; ++k){
        uint32_t lin_q20 = s_lin_out_q20[k];
        uint32_t idx12   = (lin_q20 >> LIN_Q);
        uint32_t frac8   = (lin_q20 & ((1u<<LIN_Q)-1u));
        if (idx12 > LIN_MAX) idx12 = LIN_MAX;
        uint32_t p0 = s_gamma[idx12];
        uint32_t p1 = s_gamma[(idx12 < LIN_MAX) ? idx12+1 : LIN_MAX];
        uint32_t pwm = ( (p0*(256u - frac8) + p1*frac8 + 128u) >> 8 );
        uint64_t base_q16 = ((uint64_t)pwm << 16);
        uint64_t scaled   = (base_q16 * (uint64_t)permille + 500ull) / 1000ull;
        if (scaled > ((uint64_t)PWM_MAX<<16)) scaled = ((uint64_t)PWM_MAX<<16);
        s_out_q16[k] = (uint32_t)scaled;
    }
}


static inline void LED_Core_1kHz(void)
{
    LED_RenderOneTick();
}









static void fill_targets_lin_q20_from_pos(uint32_t pos_q88){
    /* 타깃 버퍼 초기화 */
    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    uint32_t i = (pos_q88 >> QPOS_SHIFT);                // 0..8
    int kmin = (i==0)        ? 0        : (int)i-1;
    int kmax = (i>=NLED-1)   ? NLED-1   : (int)i+2;      // i+2까지

    uint32_t spread_q = (s_spread_q8 < 64) ? 64 : s_spread_q8; // ≥0.25칸

    uint16_t wq15[4]; uint8_t idx[4]; uint32_t wsum=0; int n=0;

    for (int k=kmin; k<=kmax; ++k){
        int32_t d_q = ((int32_t)k<<QPOS_SHIFT) - (int32_t)pos_q88;
        if (d_q < 0) d_q = -d_q;

        /* Q12 인덱스(4-bit 분수)로 256-LUT 보간 */
        uint32_t dnorm_q12 = ( ( (uint32_t)d_q << 12 ) + (spread_q>>1) ) / spread_q;
        if (dnorm_q12 > (255u<<4)) dnorm_q12 = (255u<<4);

        uint32_t idx8  = (dnorm_q12 >> 4);     // 0..255
        uint32_t frac4 = (dnorm_q12 & 0xFu);   // 0..15

        uint16_t w0 = s_hann256[idx8];
        uint16_t w1 = (idx8 < 255u) ? s_hann256[idx8+1] : 0;
        uint16_t w  = (uint16_t)(( (uint32_t)w0*(16u-frac4) + (uint32_t)w1*frac4 + 8u ) >> 4);

        if (w){ wq15[n]=w; idx[n]=(uint8_t)k; wsum+=w; ++n; }
    }
    if (!wsum) return;

    /* 정규화 후 선형 밝기(0..LIN_MAX), Q12.8로 스케일 업(<<8) */
    for (int t=0; t<n; ++t){
        uint32_t lin = ( (uint32_t)wq15[t] * LIN_MAX + (wsum>>1) ) / wsum;  // 0..4096
        uint32_t lin_q20 = (lin << LIN_Q);                                  // Q12.8
        s_lin_tgt_q20[idx[t]] = lin_q20;
    }
}


static inline void Pattern_SweepL2R(uint32_t pos_q88)
{
    /* (A) 전체 스윕 곡선 */
    if (g_led_ease_sweep_on){
        uint32_t limit_q = (NLED << QPOS_SHIFT);
        uint32_t t8 = pos_q88 / NLED;          // 0..255 (UDIV 1회)
        uint32_t eased8 = s_ease256[t8];       // LUT
        uint32_t eased_q16 = (eased8 << 8);
        pos_q88 = ((eased_q16 * limit_q) + 32768u) >> 16;
    }

    /* (B) 칸 내부 분수 곡선 */
    if (g_led_ease_local_on){
        uint32_t i_int = (pos_q88 >> QPOS_SHIFT);
        uint32_t f8    = (pos_q88 & ((1u<<QPOS_SHIFT)-1u));
        uint32_t fe8   = s_ease256[f8];
        pos_q88 = (i_int << QPOS_SHIFT) | fe8;
    }

    /* 최종 분배 */
    fill_targets_lin_q20_from_pos(pos_q88);
}

/* ========================================================================== */
/* 바 모드: 좌→우 채움(최대 2 LED 점등) — MasterVolume(0..50)               */
/* - 스윕 없이 현재 값만 반영.                                                */
/* - g_led_ease_local_on=1이면 분수(0..255)에 ease 적용(s_ease256).           */
/* - 값==0 → 모두 off, 값==50 → 9개 모두 on.                                   */
/* - 그 외에는 (i-1)=풀밝기, (i)=분수밝기로 "최대 2개"만 점등.                */
/* ========================================================================== */
static inline void LED_Bar_FromMasterVolume(void)
{
    /* 이 기능에서는 칸 내부 분수 곡선 사용(원하면 0으로 바꿔도 됨) */
    g_led_ease_local_on = 1;

    /* 단계 클램프 */
    uint16_t step = MasterVolume;
    if (step > 50) step = 50;

    /* 타깃 초기화 */
    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    if (step == 0) {
        /* 모두 off */
        return;
    }
    if (step == 50) {
        /* 모두 on (풀밝기) */
        uint32_t full_q20 = (LIN_MAX << LIN_Q);
        for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = full_q20;
        return;
    }

    /* 스케일: step(0..50) → x(0..NLED*50) */
    uint32_t x = (uint32_t)step * (uint32_t)NLED;   /* 0..450 */
    uint32_t i = x / 50;                             /* 현재 LED 인덱스 0..8 (step<50이므로 9는 아님) */
    uint32_t rem = x - i * 50;                       /* 0..49 (부분 진행) */

    /* 분수 0..255 계산(+ 라운딩). local ease 켜져 있으면 s_ease256로 매핑 */
    uint32_t frac8 = (rem * 255u + 25u) / 50u;       /* 0..255 */
    if (g_led_ease_local_on) {
        frac8 = s_ease256[frac8];                    /* ease-in-out 적용(더 부드러운 채움) */
    }

    /* 선형밝기 도메인에서 타깃 계산 (감마는 후단에서 적용됨) */
    uint32_t full_q20 = (LIN_MAX << LIN_Q);
    uint32_t part_lin = ( (uint32_t)LIN_MAX * frac8 + 127u ) / 255u; /* 0..LIN_MAX */
    uint32_t part_q20 = (part_lin << LIN_Q);

    if (i == 0) {
        /* 첫 LED만 부분 점등 */
        s_lin_tgt_q20[0] = part_q20;
    } else {
        /* (i-1)=풀, (i)=부분 — 최대 2개 */
        s_lin_tgt_q20[i-1] = full_q20;
        s_lin_tgt_q20[i]   = part_q20;
    }
}

/* ========================================================================== */
/* 바 모드: 좌→우 채움(최대 2 LED 점등) — SoundBalance(0..50)                */
/* 나머지 동작은 위와 동일, 데이터 소스만 SoundBalance로 변경                */
/* ========================================================================== */
static inline void LED_Bar_FromSoundBalance(void)
{
    g_led_ease_local_on = 1;

    uint16_t step = SoundBalance;
    if (step > 50) step = 50;

    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    if (step == 0) return;
    if (step == 50) {
        uint32_t full_q20 = (LIN_MAX << LIN_Q);
        for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = full_q20;
        return;
    }

    uint32_t x = (uint32_t)step * (uint32_t)NLED;   /* 0..450 */
    uint32_t i = x / 50;                             /* 0..8 */
    uint32_t rem = x - i * 50;                       /* 0..49 */

    uint32_t frac8 = (rem * 255u + 25u) / 50u;       /* 0..255 */
    if (g_led_ease_local_on) {
        frac8 = s_ease256[frac8];
    }

    uint32_t full_q20 = (LIN_MAX << LIN_Q);
    uint32_t part_lin = ( (uint32_t)LIN_MAX * frac8 + 127u ) / 255u;
    uint32_t part_q20 = (part_lin << LIN_Q);

    if (i == 0) {
        s_lin_tgt_q20[0] = part_q20;
    } else {
        s_lin_tgt_q20[i-1] = full_q20;
        s_lin_tgt_q20[i]   = part_q20;
    }
}

/* ========================================================================== */
/* 바 모드: 좌→우 채움(최대 2 LED 점등) — MetronomeVolume(0..50)            */
/* - 동작은 MasterVolume/SoundBalance와 동일 (소스 변수만 다름)               */
/* ========================================================================== */
static inline void LED_Bar_FromMetronomeVolume(void)
{
    g_led_ease_local_on = 1;

    uint16_t step = MetronomeVolume;
    if (step > 50) step = 50;

    // 타깃 초기화
    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    if (step == 0) return;

    if (step == 50) {
        uint32_t full_q20 = (LIN_MAX << LIN_Q);
        for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = full_q20;
        return;
    }

    // 0..50 → 0..NLED*50 스케일
    uint32_t x   = (uint32_t)step * (uint32_t)NLED;  /* 0..450 */
    uint32_t i   = x / 50;                           /* 0..8   */
    uint32_t rem = x - i * 50;                       /* 0..49  */

    // 칸 내부 분수(0..255) + 이징
    uint32_t frac8 = (rem * 255u + 25u) / 50u;       /* 0..255 */
    if (g_led_ease_local_on) {
        frac8 = s_ease256[frac8];
    }

    uint32_t full_q20 = (LIN_MAX << LIN_Q);
    uint32_t part_lin = ( (uint32_t)LIN_MAX * frac8 + 127u ) / 255u;
    uint32_t part_q20 = (part_lin << LIN_Q);

    if (i == 0) {
        s_lin_tgt_q20[0] = part_q20;
    } else {
        s_lin_tgt_q20[i-1] = full_q20;
        s_lin_tgt_q20[i]   = part_q20;
    }
}


/* ========================================================================== */
/* 패턴: 브리딩(중앙 LED 기준, 좌우로 퍼졌다 되돌아오는 호흡 느낌)           */
/* - 외부 변수 영향 없음(자체 위상: 들숨/홀드/날숨/휴식)                       */
/* - 중앙(인덱스 4)이 가장 밝고, 멀수록 부드럽게 낮아짐                        */
/* - 느리고 넓게: 호흡 페이즈에 따라 밝기(a)와 퍼짐(W)을 동시에 변화          */
/* ========================================================================== */
static inline void Pattern_Breath_Center(void)
{
    /* 이 모드에선 칸 내부 분수 곡선은 사용하지 않음 */
    g_led_ease_local_on = 0;

    /* 타깃 초기화 */
    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    /* 1) 시간 누적 (1kHz → +1ms), 한 사이클마다 래핑 */
    s_breath_t_ms += 1u;
    if (s_breath_t_ms >= BREATH_TOTAL_MS) s_breath_t_ms = 0u;

    /* 2) 페이즈별 선형 진폭 a_lin ∈ [0..32767] (Q15) */
    uint32_t t = s_breath_t_ms;
    uint32_t a_q15;  /* 0..32767 */
    if (t < BREATH_IN_MS) {
        /* 들이쉼: 0 → 1 */
        a_q15 = (uint32_t)(((uint64_t)t * 32767u + (BREATH_IN_MS/2)) / BREATH_IN_MS);
    } else if (t < BREATH_IN_MS + BREATH_HOLD_MS) {
        /* 고점 홀드 */
        a_q15 = 32767u;
    } else if (t < BREATH_IN_MS + BREATH_HOLD_MS + BREATH_OUT_MS) {
        /* 내쉼: 1 → 0 */
        uint32_t ti = t - (BREATH_IN_MS + BREATH_HOLD_MS);
        a_q15 = (uint32_t)(((uint64_t)(BREATH_OUT_MS - ti) * 32767u + (BREATH_OUT_MS/2)) / BREATH_OUT_MS);
    } else {
        /* 저점 휴식 */
        a_q15 = 0u;
    }

    /* 3) 진폭 이징: a8 = s_ease256[a8_lin]  (시작/끝 더 부드럽게) */
    uint32_t a8_lin = (uint32_t)(((uint64_t)a_q15 * 255u + 16383u) / 32767u);  /* 0..255 */
    uint32_t a8     = s_ease256[a8_lin];                                       /* 0..255 */


    //**********************************
    /* 4) 공간 퍼짐 W: a=0일 때도 넓게 보이도록 범위 ↑ (기존 0.5~2.0 → 0.7~3.0 LED) */
    const uint32_t W_MIN_Q8 = 170u;   /* 179=0.7 LED */
    const uint32_t W_MAX_Q8 = 1400u;   /* 768=3.0 LED */
    uint32_t W_q8 = W_MIN_Q8 + ( ((W_MAX_Q8 - W_MIN_Q8) * a8 + 127u) / 255u );
    //**********************************



    /* 5) 중심(4)에서 Hann 프로파일로 좌우 퍼짐 + 진폭 a8 반영 → 선형밝기 타깃 */
    const uint32_t center_q = (4u << QPOS_SHIFT);        /* 중앙 LED = 인덱스 4 */
    const uint64_t DEN = (uint64_t)32767u * 255u;        /* 정규화 분모(w * a) */

    for (int k=0; k<NLED; ++k){
        /* 거리(Q8.8) */
        uint32_t x_q = ((uint32_t)k << QPOS_SHIFT);
        uint32_t d_q = (x_q > center_q) ? (x_q - center_q) : (center_q - x_q);

        /* 거리 정규화 → 256-LUT 보간 인덱스(0..255, 4-bit 분수) */
        uint32_t dnorm_q12 = ( ((uint32_t)d_q << 12) + (W_q8>>1) ) / W_q8;
        if (dnorm_q12 > (255u<<4)) dnorm_q12 = (255u<<4);
        uint32_t idx8  = (dnorm_q12 >> 4);
        uint32_t frac4 = (dnorm_q12 & 0xFu);

        /* raised-cosine 가중치 (Q15) */
        uint16_t w0 = s_hann256[idx8];
        uint16_t w1 = (idx8 < 255u) ? s_hann256[idx8+1] : 0;
        uint16_t w  = (uint16_t)(((uint32_t)w0 * (16u - frac4) + (uint32_t)w1 * frac4 + 8u) >> 4);

        /* 선형 밝기 = (w/32767) * (a8/255) * LIN_MAX  (정수 스케일) */
        uint64_t num = (uint64_t)w * (uint64_t)a8 * (uint64_t)LIN_MAX + (DEN/2); /* 반올림 */
        uint32_t lin = (uint32_t)( num / DEN );  /* 0..LIN_MAX */

        s_lin_tgt_q20[k] = (lin << LIN_Q);  /* Q12.8 → 후단 스무딩/감마/ΣΔ */
    }
}

/* ========================================================================== */
/* 패턴: 메트로놈 동기 밴드 왕복 (2~3칸)                                      */
/*  - 박자 하나(beat)의 시간 동안:                                              */
/*     [성장] 끝에서 1칸→설정길이까지 부드럽게 늘리고                         */
/*     [이동] 설정 길이 유지하며 선형 이동                                     */
/*     [수축] 반대 끝에 닿기 직전 길이를 1칸으로 줄이며 끝 LED만 남김          */
/*  - 홀수박: L→R / 짝수박: R→L (마디 단위로 parity 재시작)                    */
/*  - Ready=0이면 좌/우 끝 LED만 절반 밝기로 유지                              */
/*  - 연산: LUT(s_ease256) + 정수만 사용                                       */
/* ========================================================================== */
/* 끝(1,9) LED 항상-켜짐 가변 */
volatile uint8_t  g_led_end_base_en = 1;           /* 0=끄기, 1=켜기 */
volatile uint16_t g_led_end_base_permille = 40;   /* 0..1000 (≈40%) */

void LED_SetEndBase(uint8_t enable, uint16_t permille){
    g_led_end_base_en = enable ? 1 : 0;
    if (permille > 1000u) permille = 1000u;
    g_led_end_base_permille = permille;
}

/* ========================================================================== */
/* 패턴: 메트로놈 동기 밴드 왕복 (연속 흐름, 끝LED 가변, BPM별 길이 소폭 가변) */
/*  - beat 위상: Q16 누산기 (φ += inc, 65536 넘어가면 wrap + beat index++)     */
/*  - 밴드 칠하기: Q8.8 연속 위치 + 커버리지(0..1) 안티에일리어싱              */
/*  - 끝 LED(1,9): g_led_end_base_en/permille 로 밝기/ON·OFF 제어              */
/*  - 길이: MET_BAND_LEDS를 기준으로 slow/fast BPM에서 ±1칸만 가변             */
/* ========================================================================== */
static inline void Pattern_MetroBand(void)
{
    g_led_ease_local_on = 0;                 /* 내부 분수 ease 비활성 (필요 없음) */

    /* 타깃 초기화 */
    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    /* 끝 LED 기본 밝기(옵션) */
    if (g_led_end_base_en){
        uint32_t base_q20 = ((uint32_t)LIN_MAX * g_led_end_base_permille / 1000u) << LIN_Q;
        s_lin_tgt_q20[0]        = base_q20;
        s_lin_tgt_q20[NLED - 1] = base_q20;
    }

    /* 메트로놈 off면 종료 */
    if (!g_met_ready) return;

    /* === [핵심] 위상/방향은 메트로놈 엔진 공유값을 그대로 사용 === */
    uint32_t phi8   = (g_met_phi_q16 >> 8) & 0xFFu;            /* 0..255 */
    uint8_t  dir_rl = (g_met_beat_inbar & 1u) ? 1u : 0u;       /* 홀/짝 박으로 방향 */

    /* 길이 가변은 기존과 동일(= BPM 참조) */
    uint16_t bpm = MetronomeBPM ? MetronomeBPM : 60;

    /* --- [B] 스테이지 비율: 성장/이동/수축 --- */
    uint32_t G = MET_GROW_FRAC_Q8;           /* 0..255 */
    uint32_t S = MET_SHRINK_FRAC_Q8;
    if (G + S > 220u) S = 220u - G;          /* 이동 구간 최소 확보 */
    uint32_t MOVE = 255u - (G + S);

    /* --- [C] 길이 가변: 느리면 +1, 빠르면 -1 (최대 ±1칸) --- */
    uint32_t L = MET_BAND_LEDS;
    if (bpm <= MET_LEN_SLOW_BPM && MET_LEN_DELTA_MAX){
        uint32_t add = MET_LEN_DELTA_MAX;
        if (L + add > NLED) add = (NLED > L) ? (NLED - L) : 0;
        L += add;
    } else if (bpm >= MET_LEN_FAST_BPM && MET_LEN_DELTA_MAX){
        uint32_t sub = MET_LEN_DELTA_MAX;
        if (L > sub) L -= sub; else L = 1;
    }
    if (L < 1u)   L = 1u;
    if (L > NLED) L = NLED;

    /* --- [E] 밴드 tail/len (Q8.8) — 연속 이동 --- */
    uint32_t len_q8, tail_q8;                /* 좌→우 기준 */
    if (phi8 <= G){
        /* 성장: 1칸 → L칸 (ease) */
        uint32_t t8 = G ? ((phi8 * 255u + (G/2u)) / G) : 255u;
        if (t8 > 255u) t8 = 255u;
        uint32_t a8 = s_ease256[t8];
        len_q8  = 256u * ( 1u + ((L - 1u) * a8 + 254u) / 255u );
        tail_q8 = 0u;
    }else if (phi8 < (255u - S)){
        /* 이동: 길이 고정, 꼬리 0→(NLED-L) 선형 */
        uint32_t phi_move8 = ( ((phi8 - G) * 255u) + (MOVE/2u) ) / (MOVE ? MOVE : 1u);
        uint32_t travel_q8 = 256u * (NLED - L);
        tail_q8 = (travel_q8 * phi_move8 + 254u) / 255u;
    }else{
        /* 수축: L칸 → 1칸 (ease), 반대 끝에서 꼬리 고정 */
        uint32_t t8 = S ? (((phi8 - (255u - S)) * 255u + (S/2u)) / S) : 255u;
        if (t8 > 255u) t8 = 255u;
        uint32_t a8 = s_ease256[t8];
        len_q8  = 256u * ( L - ((L - 1u) * a8 + 254u) / 255u );
        if (len_q8 < 256u) len_q8 = 256u;
        tail_q8 = 256u * (NLED) - len_q8;    /* 좌→우 기준 끝붙임 */
    }

    /* 방향 미러링(R→L) */
    uint32_t band_start_q8 =
        (dir_rl == 0u) ? tail_q8 : (256u * NLED - tail_q8 - len_q8);
    uint32_t band_end_q8 = band_start_q8 + len_q8;

    /* --- [F] 커버리지로 칠하기(안티에일리어싱) --- */
    for (uint32_t k=0; k<NLED; ++k){
        uint32_t led_s = 256u * k;
        uint32_t led_e = led_s + 256u;

        uint32_t s_max = (band_start_q8 > led_s) ? band_start_q8 : led_s;
        uint32_t e_min = (band_end_q8   < led_e) ? band_end_q8   : led_e;
        int32_t  cov_q8 = (int32_t)e_min - (int32_t)s_max;

        if (cov_q8 > 0){
            uint32_t lin = ((uint32_t)LIN_MAX * (uint32_t)cov_q8 + 128u) >> 8;
            uint32_t v_q20 = (lin << LIN_Q);
            if (v_q20 > s_lin_tgt_q20[k]) s_lin_tgt_q20[k] = v_q20;
        }
    }
}

/* ==========================================================================
 * 메트로놈 싱글 LED 패턴
 * - 메트로놈 켜질 때마다 위상 0, 마디도 0 → 항상 끝에서 시작
 * - 한 박 길이 = BPM & TimeSignatureDen 으로 계산
 * - 한 박 동안 0→(NLED-1) 까지 선형 이동
 * - 박 경계에서 마디 박자 +1 하고, 그때 방향 토글
 *   → 1,3,5...번 박(L→R) / 2,4,6...번 박(R→L)
 * - 확산/밴드 없음: LED 1개만 켜짐
 * ========================================================================== */
static inline void Pattern_MetroSingleBeat(void)
{
    g_led_ease_local_on = 0;

    /* 기본적으로 전부 끄고 시작 */
    for (int k = 0; k < NLED; ++k)
        s_lin_tgt_q20[k] = 0;

    /* 메트로놈 off면 종료 */
    if (!g_met_ready) return;

    /* === [핵심] 위상/방향은 메트로놈 엔진 공유값을 그대로 사용 === */
    uint32_t phi_q16 = g_met_phi_q16;               /* 0..65535 */
    uint8_t  dir_rl  = (g_met_beat_inbar & 1u) ? 1u : 0u;

    /* 현재 위상을 0..NLED-1 로 매핑 (반올림) */
    uint32_t pos = 0;
    if (NLED > 1) {
        pos = ( (uint64_t)phi_q16 * (uint32_t)(NLED - 1u) + 32768u ) >> 16;
    }

    uint8_t led_idx;
    if (dir_rl == 0u) {
        /* L→R */
        led_idx = (uint8_t)pos;
    } else {
        /* R→L */
        led_idx = (uint8_t)((NLED - 1u) - pos);
    }

    /* 이 LED 하나만 풀밝기 */
    s_lin_tgt_q20[led_idx] = ((uint32_t)LIN_MAX << LIN_Q);
}



/* ==========================================================================
 * SoundGen 표시 (A0..A7 → LED1..LED9 매핑)
 * - 바 규칙(최대 2 LED 점등): (i-1)=풀밝기, (i)=분수밝기
 * - IsSoundGenReady==1이면 현재 위치 LED(1~2개)에 블링크(최소↔최대) 적용
 * - Ready==0이면 깜빡임 없이 고정 표시
 * ========================================================================= */
/* === SoundGen 표시용 블링크 설정 ================================== */
/* 1 사이클(최대→최소→최대) 기간(ms) — 기본 2000ms */
volatile uint16_t g_sg_blink_period_ms = 2000;
/* 최소/최대 밝기(%) — 기본 20% ~ 100%  */
volatile uint16_t g_sg_blink_min_pct = 20;   /* 0..100 */
volatile uint16_t g_sg_blink_max_pct = 100;  /* 0..100 */

/* 내부 LFO 상태(ms 누적, 1kHz 틱 기준) */
static uint32_t s_sg_lfo_t_ms = 0;



/* === REPLACE THIS FUNCTION ONLY ============================================
 * 완전 선형 보간 버전:
 * - NoteNames[]의 첫 원소 ↔ 마지막 원소 전체를 0..(NLED-1)로 선형 매핑
 * ========================================================================== */
static inline void LED_Bar_FromNoteIndex(void)
{
    /* 칸 내부 분수 이징은 끈다: 완전 선형 보간을 위해 */
    g_led_ease_local_on = 0;

    /* 타깃 초기화 */
    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    /* 인덱스: 0..(SG_NOTE_COUNT-1) */
    uint32_t idx = (uint32_t)CurrentNoteIndex;
    if (idx >= SG_NOTE_COUNT) idx = SG_NOTE_COUNT - 1u;

    /* 전 구간을 0..(NLED-1)*256 으로 선형 매핑 (Q8.8 위치) */
    const uint32_t first = 0u;
    const uint32_t last  = SG_NOTE_COUNT - 1u;
    uint32_t den    = (last - first);             /* SG_NOTE_COUNT>=2 전제 */
    if (den == 0u) den = 1u;                      /* 안전장치 */

    uint32_t pos_q8 = ( ((idx - first) * ((uint32_t)(NLED - 1) * 256u)) + (den>>1) ) / den; /* 반올림 */
    uint32_t i      = (pos_q8 >> 8);              /* 0..(NLED-1) */
    uint32_t frac8  = (pos_q8 & 0xFFu);           /* 0..255 */

    /* 선형 가중치: 왼쪽=1-frac, 오른쪽=frac (항상 연속) */
    uint32_t left  = i;
    uint32_t right = (i < (uint32_t)(NLED - 1)) ? (i + 1u) : i;

    uint32_t wL = 256u - frac8;                   /* 1..256 (pos 정수면 256) */
    uint32_t wR = frac8;                          /* 0..255  */

    const uint32_t full_lin = LIN_MAX;

    /* 왼쪽 LED */
    if (left < NLED) {
        uint32_t linL   = (full_lin * wL + 128u) >> 8;   /* 0..LIN_MAX */
        s_lin_tgt_q20[left] = (linL << LIN_Q);
    }
    /* 오른쪽 LED (left==right이면 생략) */
    if (right < NLED && right != left) {
        uint32_t linR   = (full_lin * wR + 128u) >> 8;
        s_lin_tgt_q20[right] = (linR << LIN_Q);
    }

    /* === Ready==1: 현재 점등된 그 LED들만 부드러운 깜빡임 스케일 적용 === */
    if (IsSoundGenReady) {
        /* 1) LFO 진행 (삼각파 0..1..0) */
        uint32_t P = (uint32_t)g_sg_blink_period_ms;
        if (P < 10u) P = 10u;
        s_sg_lfo_t_ms += 1u;
        if (s_sg_lfo_t_ms >= P) s_sg_lfo_t_ms -= P;

        uint32_t half   = P >> 1;
        uint32_t up     = (s_sg_lfo_t_ms <= half) ? s_sg_lfo_t_ms : (P - s_sg_lfo_t_ms);
        uint32_t tri_pm = ( (up * 1000u) + (half/2u) ) / (half ? half : 1u); /* 0..1000 */

        /* 2) 스케일(permille): min%..max% */
        uint32_t min_pm = ((g_sg_blink_min_pct > 100u) ? 100u : g_sg_blink_min_pct) * 10u;
        uint32_t max_pm = ((g_sg_blink_max_pct > 100u) ? 100u : g_sg_blink_max_pct) * 10u;
        if (max_pm < min_pm) { uint32_t tmp=min_pm; min_pm=max_pm; max_pm=tmp; }
        uint32_t scale_pm = min_pm + ( ((max_pm - min_pm) * tri_pm + 500u) / 1000u );

        /* 3) 실제 점등된 인덱스에만 스케일 적용 (0이 아닌 항목만) */
        if (s_lin_tgt_q20[left]) {
            uint64_t v = ((uint64_t)s_lin_tgt_q20[left] * (uint64_t)scale_pm + 500ull) / 1000ull;
            s_lin_tgt_q20[left] = (uint32_t)v;
        }
        if (right != left && s_lin_tgt_q20[right]) {
            uint64_t v = ((uint64_t)s_lin_tgt_q20[right] * (uint64_t)scale_pm + 500ull) / 1000ull;
            s_lin_tgt_q20[right] = (uint32_t)v;
        }
    }
}



/* ========================================================================== */
/* 튜너 패턴(Hz기준 IDLE, 5번 40% 고정, 두-LED 선형 보간)                     */
/*  - IDLE(Hz==0): LED5만 40% (정확히 한 칸만 점등, 잔광 클리어)               */
/*  - ACTIVE(Hz>0): 항상 두 칸(n, n+1)만 선형 보간 (감마/스무딩은 후단 그대로) */
/*      · cents == 0     → LED5=100% 단독                                     */
/*      · cents <= -50   → 모두 OFF                                           */
/*      · cents >= +50   → 모두 OFF                                           */
/*      · (-50,0)        → 좌측 4칸 내부 보간                                 */
/*      · (0,+50)        → 우측 4칸 내부 보간                                 */
/* ========================================================================== */
// LEDcontrol.c
/* === TUNER 애니메이션 확장 블록 ===========================================
 * 기존 구조:
 *   1) 튜너 원시값 → LED 9개 밝기 계산 (좌4 / 가운데 / 우4)
 *   2) s_lin_tgt_q20[] 에 써서 뒤 파이프라인(감마/EMA/ΣΔ)로 넘김
 *
 * 아래 블록은 1) 을 먼저 '임시 버퍼'에 계산한 뒤,
 * 그 결과를 상태머신으로 애니메이션해서 s_lin_tgt_q20[] 에 최종 기록한다.
 * ========================================================================== */

typedef enum {
    TNR_ANIM_STEADY = 0,    // 변화 없음: 계산된 패턴 그대로
    TNR_ANIM_APPEAR,        // 없었다가 생김: 빠르게→느리게
    TNR_ANIM_DISAPPEAR,     // 있다가 사라짐: 느리게→빠르게
    TNR_ANIM_JUMP           // 값이 튐: 느→빠→느 (짧게)
} tnr_anim_state_t;

static struct {
    tnr_anim_state_t st;
    uint8_t  prev_has_sig;            // 직전 프레임에 신호 있었는지
    uint32_t elapsed_ms;              // 이번 애니 경과
    uint32_t dur_ms;                  // 이번 애니 길이
    uint32_t prev_q20[9];             // 애니 시작 시점 LED 밝기
    uint32_t next_q20[9];             // 애니 끝났을 때 LED 밝기(=원래 패턴)
    int16_t  last_cent;               // 직전 센트값(점프 감지용)
} s_tnr_anim = {
    .st = TNR_ANIM_STEADY,
    .prev_has_sig = 0,
    .elapsed_ms = 0,
    .dur_ms = 0,
    .last_cent = 0
};

/* cubic 이징 0..255 → 0..255 */
static inline uint32_t ease_out_cubic_255(uint32_t t)
{
    /* 1 - (1-t)^3 */
    if (t >= 255u) return 255u;
    uint32_t inv = 255u - t;
    uint32_t inv2 = (inv * inv + 127u) / 255u;
    uint32_t inv3 = (inv2 * inv + 127u) / 255u;
    return 255u - inv3;
}

static inline uint32_t ease_in_cubic_255(uint32_t t)
{
    /* t^3 */
    if (t >= 255u) return 255u;
    uint32_t t2 = (t * t + 127u) / 255u;
    uint32_t t3 = (t2 * t + 127u) / 255u;
    return t3;
}

static inline uint32_t ease_inout_cubic_255(uint32_t t)
{
    if (t >= 255u) return 255u;
    if (t <= 127u) {
        /* 앞 절반: ease-in */
        uint32_t tt = (t * 2u);                 // 0..254
        uint32_t tt2 = (tt * tt + 127u) / 255u;
        uint32_t tt3 = (tt2 * tt + 127u) / 255u;
        return (tt3 >> 1);                      // /2
    } else {
        /* 뒤 절반: ease-out */
        uint32_t tt = (t - 127u) * 2u;          // 0..256 근처
        if (tt > 255u) tt = 255u;
        uint32_t out = ease_out_cubic_255(tt);
        return (out >> 1) + 127u;
    }
}

/* 원래 Pattern_Tuner_CentTwoLEDs() 가 하던 일을
 * 'dst[]'라는 임시 버퍼에만 해두는 함수.
 * has_sig=1 이면 실제 튜너 값이 있는 상태, 0이면 무음이다. */
static inline void tuner_build_base_frame_q20(uint32_t *dst, uint8_t *has_sig_out)
{
    extern volatile int32_t TunerMeasurement_x10;  // 0.1Hz 단위 (무음=0)
    extern volatile uint16_t CurrentTunerCent;     // -50..+50 이지만 uint로 들어옴

    /* 0) 초기화 */
    for (int k = 0; k < NLED; ++k) dst[k] = 0;

    /* 1) 가운데 LED 최소 30% */
    uint32_t base_pct = (uint32_t)g_tnr_center_idle_pct;
    if (base_pct < 20u) base_pct = 20u;
    if (base_pct > 100u) base_pct = 100u;
    uint32_t base_lin  = ((uint32_t)LIN_MAX * base_pct + 50u) / 100u;
    uint32_t base_q20  = (base_lin << LIN_Q);
    dst[4]             = base_q20;   // 기본값

    /* 2) 신호 없으면 여기까지가 최종 프레임 */
    if (TunerMeasurement_x10 <= 0) {
        *has_sig_out = 0;
        return;
    }
    *has_sig_out = 1;

    /* 3) 센트 기반 두-LED 보간 (원래 코드 그대로 옮김) */
    int32_t cents = (int32_t)((int16_t)CurrentTunerCent);

    if (cents <= -50 || cents >= 50) {
        /* 범위 밖이면 가운데만 */
        return;
    }

    /* 0c는 가운데 풀밝기로 덮기 */
    if (cents == 0) {
        dst[4] = (LIN_MAX << LIN_Q);
        return;
    }

    /* 한쪽 4칸을 Q8로 나눠쓰는 건 원래 코드와 동일 */
    const uint32_t HALF_SPAN_Q8 = (4u << 8);

    /* 원래 코드에 있던 LED칸 내부 ease-out */
    #define EASE_OUT_255(frac8_) ({                             \
        uint32_t _x = (uint32_t)(frac8_);                       \
        uint32_t _inv  = 255u - _x;                             \
        uint32_t _inv2 = ( (_inv * _inv) + 127u ) / 255u;       \
        uint32_t _inv3 = ( (_inv2 * _inv) + 127u ) / 255u;      \
        (uint32_t)(255u - _inv3);                               \
    })

    if (cents < 0) {
        /* (-50,0): 좌측 4칸 */
        uint32_t u      = (uint32_t)(cents + 50);               // 1..49
        uint32_t pos_q8 = (u * HALF_SPAN_Q8 + 25u) / 50u;       // 0..~1023
        uint32_t i      = (pos_q8 >> 8);                        // 0..3
        uint32_t frac8  = (pos_q8 & 0xFFu);                     // 0..255
        uint32_t e8     = EASE_OUT_255(frac8);

        const uint32_t full_lin = (uint32_t)LIN_MAX;
        uint32_t linR = (full_lin * e8 + 127u) / 255u;
        uint32_t linL = (linR > full_lin) ? 0u : (full_lin - linR);

        uint32_t left  = i;
        uint32_t right = i + 1u;

        dst[left]  = (linL << LIN_Q);
        dst[right] = (linR << LIN_Q);

        /* 가운데가 보간쌍에 걸렸으면 최소 30% 보장 */
        if (right == 4 || left == 4) {
            if (dst[4] < base_q20) dst[4] = base_q20;
        }
    } else {
        /* (0,+50): 우측 4칸 */
        uint32_t u        = (uint32_t)cents;                    // 1..49
        uint32_t delta_q8 = (u * HALF_SPAN_Q8 + 25u) / 50u;     // 0..~1023
        uint32_t pos_q8   = (4u << 8) + delta_q8;               // 1024..~2047
        uint32_t i        = (pos_q8 >> 8);                      // 4..7
        uint32_t frac8    = (pos_q8 & 0xFFu);                   // 0..255
        uint32_t e8       = EASE_OUT_255(frac8);

        const uint32_t full_lin = (uint32_t)LIN_MAX;
        uint32_t linR = (full_lin * e8 + 127u) / 255u;
        uint32_t linL = (linR > full_lin) ? 0u : (full_lin - linR);

        uint32_t left  = i;
        uint32_t right = i + 1u;

        dst[left]  = (linL << LIN_Q);
        dst[right] = (linR << LIN_Q);

        if (right == 4 || left == 4) {
            if (dst[4] < base_q20) dst[4] = base_q20;
        }
    }

    #undef EASE_OUT_255
}

/* 실제 패턴: 이걸로 기존 함수를 교체하면 된다 */
static inline void Pattern_Tuner_CentTwoLEDs(void)
{
    /* 1) 이번 틱에서 계산된 '원래' 튜너 프레임 */
    uint32_t raw_q20[9];
    uint8_t  has_sig = 0;
    tuner_build_base_frame_q20(raw_q20, &has_sig);

    /* 2) 우선 지금 애니 상태에 따라 '현재 화면'을 한 번 계산해둔다
     *    (애니 도중에 값이 바뀌면 이걸 시작점으로 다시 애니 시작해야 하니까) */
    uint32_t cur_frame[9];
    if (s_tnr_anim.dur_ms == 0 || s_tnr_anim.st == TNR_ANIM_STEADY) {
        for (int i=0;i<9;i++) cur_frame[i] = s_tnr_anim.next_q20[i];
    } else {
        uint32_t t255 = (s_tnr_anim.elapsed_ms >= s_tnr_anim.dur_ms)
                      ? 255u
                      : (s_tnr_anim.elapsed_ms * 255u) / s_tnr_anim.dur_ms;
        uint32_t e255 = 0;
        switch (s_tnr_anim.st) {
        default:
        case TNR_ANIM_STEADY:   e255 = 255u; break;
        case TNR_ANIM_APPEAR:   e255 = ease_out_cubic_255(t255);  break;
        case TNR_ANIM_DISAPPEAR:e255 = ease_in_cubic_255(t255);   break;
        case TNR_ANIM_JUMP:     e255 = ease_inout_cubic_255(t255);break;
        }
        for (int i=0;i<9;i++) {
            uint32_t v0 = s_tnr_anim.prev_q20[i];
            uint32_t v1 = s_tnr_anim.next_q20[i];
            int32_t  diff = (int32_t)v1 - (int32_t)v0;
            uint32_t v = v0 + ( (diff * (int32_t)e255) / 255 );
            cur_frame[i] = v;
        }
    }

    /* 3) 상태 전이 판단 */
    /* 이번 센트(=raw에 반영된 값)를 얻어와서 점프 크기를 본다 */
    extern volatile uint16_t CurrentTunerCent;
    int16_t this_cent = (int16_t)CurrentTunerCent;
    int16_t diff_cent = this_cent - s_tnr_anim.last_cent;
    if (diff_cent < 0) diff_cent = -diff_cent;

    if (!s_tnr_anim.prev_has_sig && has_sig) {
        /* 없었는데 생김 → appear */
        for (int i=0;i<9;i++) {
            s_tnr_anim.prev_q20[i] = 0;           // 왼쪽 바깥에서 들어오는 느낌 내려면 여기 0
            s_tnr_anim.next_q20[i] = raw_q20[i];
        }
        s_tnr_anim.st         = TNR_ANIM_APPEAR;
        s_tnr_anim.elapsed_ms = 0;
        s_tnr_anim.dur_ms     = 200;              // 0.2s
    }
    else if (s_tnr_anim.prev_has_sig && !has_sig) {
        /* 있었는데 사라짐 → disappear */
        for (int i=0;i<9;i++) {
            s_tnr_anim.prev_q20[i] = cur_frame[i];   // 지금 상태에서부터
            s_tnr_anim.next_q20[i] = 0;              // 전부 0 (중앙 최소값은 후단에서 한 번 더 깔린 상태라 괜찮음)
        }
        s_tnr_anim.st         = TNR_ANIM_DISAPPEAR;
        s_tnr_anim.elapsed_ms = 0;
        s_tnr_anim.dur_ms     = 500;                 // 0.5s
    }
    else if (has_sig) {
        /* 신호는 있는데 값이 바뀐 상황 */
        /* 센트가 조금만 움직였으면 그냥 next만 덮어쓰고,
           많이 튄 거면 jump 애니로 한 번 더 보여준다. 기준은 ±4c 정도로 둔다. */
        if (diff_cent > 4) {
            /* jump: 지금 그려진 화면 → 새 패턴, 짧게 */
            for (int i=0;i<9;i++) {
                s_tnr_anim.prev_q20[i] = cur_frame[i];
                s_tnr_anim.next_q20[i] = raw_q20[i];
            }
            s_tnr_anim.st         = TNR_ANIM_JUMP;
            s_tnr_anim.elapsed_ms = 0;
            s_tnr_anim.dur_ms     = 120;            // 아주 빠르게
        } else {
            /* 작은 이동이면 steady로 두고 다음 프레임만 업데이트 */
            for (int i=0;i<9;i++) {
                s_tnr_anim.next_q20[i] = raw_q20[i];
            }
            if (s_tnr_anim.st == TNR_ANIM_STEADY) {
                /* 바로 쓴다 */
            } else {
                /* 진행 중이면 그대로 두고 아래에서 보간됨 */
            }
        }
    }

    /* 4) 시간 진행 (이 패턴은 1kHz에서 불리니까 dt=1ms 가정) */
    if (s_tnr_anim.st != TNR_ANIM_STEADY) {
        if (s_tnr_anim.elapsed_ms < s_tnr_anim.dur_ms)
            s_tnr_anim.elapsed_ms += 1;
        if (s_tnr_anim.elapsed_ms > s_tnr_anim.dur_ms)
            s_tnr_anim.elapsed_ms = s_tnr_anim.dur_ms;
    }

    /* 5) 최종 보간해서 실제 타깃 버퍼에 쓴다 */
    uint32_t t255 = 255u;
    if (s_tnr_anim.st != TNR_ANIM_STEADY && s_tnr_anim.dur_ms > 0) {
        t255 = (s_tnr_anim.elapsed_ms * 255u) / s_tnr_anim.dur_ms;
    }
    uint32_t e255 = 255u;
    switch (s_tnr_anim.st) {
    default:
    case TNR_ANIM_STEADY:    e255 = 255u; break;
    case TNR_ANIM_APPEAR:    e255 = ease_out_cubic_255(t255);   break;
    case TNR_ANIM_DISAPPEAR: e255 = ease_in_cubic_255(t255);    break;
    case TNR_ANIM_JUMP:      e255 = ease_inout_cubic_255(t255); break;
    }

    for (int i=0;i<9;i++) {
        uint32_t v0 = s_tnr_anim.prev_q20[i];
        uint32_t v1 = s_tnr_anim.next_q20[i];
        int32_t  diff = (int32_t)v1 - (int32_t)v0;
        uint32_t v = v0 + ( (diff * (int32_t)e255) / 255 );
        s_lin_tgt_q20[i] = v;
    }

    /* 6) 애니 끝났으면 steady로 고정해두기 */
    if (s_tnr_anim.st != TNR_ANIM_STEADY &&
        s_tnr_anim.elapsed_ms >= s_tnr_anim.dur_ms)
    {
        s_tnr_anim.st = TNR_ANIM_STEADY;
        /* 화면과 next를 일치시켜 둔다 */
        for (int i=0;i<9;i++) {
            s_tnr_anim.prev_q20[i] = s_tnr_anim.next_q20[i];
        }
    }

    /* 7) 다음 프레임용 플래그/센트 기억 */
    s_tnr_anim.prev_has_sig = has_sig;
    s_tnr_anim.last_cent    = this_cent;
}
/* === TUNER 애니메이션 확장 블록 끝 ====================================== */


/* ============================================================================
 * Clip/Overlevel Warn — 좌(I2S2): LED 0..2, 우(I2S3): LED 6..8
 *  - seg30>=29(≈−3 dBFS 초과)가 '지속'될 때만 점멸
 *  - 강 EMA(UP/DN 분리) + HOLD + 느린 BLINK로 과도한 깜빡임 방지
 * ============================================================================ */
// 외부 getter 선언(파일 상단 extern 섹션에 이미 없으면 추가)
// extern void notch_get_vu_segments(uint8_t *outSegL, uint8_t *outSegR);
// extern void notch_get_mic_vu_segments(uint8_t *seg_out);

/* ============================================================================
 * Clip/Overlevel Warn — 좌(I2S2): LED 0..3, 우(I2S3): LED 5..8
 * - -3 dBFS(=seg30≥29)가 '충분히 오래' 지속될 때만 점멸
 * - TOT(Time-Over-Threshold) 누적 + on/off 시간 히스테리시스로 과민 반응 억제
 * ============================================================================ */
static inline void LED_Pattern_ClipWarn(void)
{
    // 0) 초기화
    for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;

    // 1) 입력(seg30) 읽기
    uint8_t segL = 0, segR = 0, segM = 0;
    notch_get_vu_segments(&segL, &segR);   // I2S2 L/R
    notch_get_mic_vu_segments(&segM);      // I2S3 mono
    uint8_t segTrack = (segL > segR) ? segL : segR; // 좌측은 스테레오 최대

    // 2) 임계 초과 여부
    uint8_t overL = (segTrack >= CLIP_SEG_TH);
    uint8_t overR = (segM     >= CLIP_SEG_TH);

    // 3) TOT 누적/감쇠(지속성 계산)
    if (overL) {
        if (s_over_ms_L < 1000u) s_over_ms_L = (uint16_t)(s_over_ms_L + LED_FRAME_MS);
        if (!s_active_L && s_over_ms_L >= CLIP_ON_TIME_MS) {
            s_active_L = 1;
            s_blink_tmr_L = 0; // 새 사이클
        }
    } else {
        if (s_over_ms_L > CLIP_DECAY_PER_FRAME_MS) s_over_ms_L -= CLIP_DECAY_PER_FRAME_MS;
        else s_over_ms_L = 0;
        if (s_active_L && s_over_ms_L <= CLIP_OFF_TIME_MS) s_active_L = 0;
    }

    if (overR) {
        if (s_over_ms_R < 1000u) s_over_ms_R = (uint16_t)(s_over_ms_R + LED_FRAME_MS);
        if (!s_active_R && s_over_ms_R >= CLIP_ON_TIME_MS) {
            s_active_R = 1;
            s_blink_tmr_R = 0;
        }
    } else {
        if (s_over_ms_R > CLIP_DECAY_PER_FRAME_MS) s_over_ms_R -= CLIP_DECAY_PER_FRAME_MS;
        else s_over_ms_R = 0;
        if (s_active_R && s_over_ms_R <= CLIP_OFF_TIME_MS) s_active_R = 0;
    }

    // 4) 점멸 페이즈(활성 상태에서만)
    if (s_active_L) s_blink_tmr_L = (uint16_t)((s_blink_tmr_L + LED_FRAME_MS) % CLIP_BLINK_PERIOD_MS);
    if (s_active_R) s_blink_tmr_R = (uint16_t)((s_blink_tmr_R + LED_FRAME_MS) % CLIP_BLINK_PERIOD_MS);
    uint8_t onL = (s_active_L && (s_blink_tmr_L < CLIP_BLINK_ON_MS));
    uint8_t onR = (s_active_R && (s_blink_tmr_R < CLIP_BLINK_ON_MS));

    // 5) 렌더(좌 0..3 / 우 5..8)
    const uint32_t FULL_Q20 = (LIN_MAX << LIN_Q);
    const uint32_t WARN_Q20 = FULL_Q20; // 필요하면 3/4 밝기 등으로 낮춰도 됨

    if (onL) {
        if (NLED >= 4) {
            s_lin_tgt_q20[0] = WARN_Q20;
            s_lin_tgt_q20[1] = WARN_Q20;
            s_lin_tgt_q20[2] = WARN_Q20;
            s_lin_tgt_q20[3] = WARN_Q20;
        } else if (NLED >= 3) {
            s_lin_tgt_q20[0] = WARN_Q20;
            s_lin_tgt_q20[1] = WARN_Q20;
            s_lin_tgt_q20[2] = WARN_Q20;
        }
    }
    if (onR) {
        if (NLED >= 9) {
            s_lin_tgt_q20[5] = WARN_Q20;
            s_lin_tgt_q20[6] = WARN_Q20;
            s_lin_tgt_q20[7] = WARN_Q20;
            s_lin_tgt_q20[8] = WARN_Q20;
        } else if (NLED >= 6) {
            s_lin_tgt_q20[NLED-4] = WARN_Q20;
            s_lin_tgt_q20[NLED-3] = WARN_Q20;
            s_lin_tgt_q20[NLED-2] = WARN_Q20;
            s_lin_tgt_q20[NLED-1] = WARN_Q20;
        } else if (NLED >= 3) {
            s_lin_tgt_q20[NLED-3] = WARN_Q20;
            s_lin_tgt_q20[NLED-2] = WARN_Q20;
            s_lin_tgt_q20[NLED-1] = WARN_Q20;
        }
    }

    // 나머지 이징/감마/ΣΔ는 기존 RenderOneTick() 경로에서 처리
}



/* === 패턴 디스패처 ================================================== */
inline void LED_PatternDispatch(uint32_t pos_q88)
{
    switch (g_led_mode_sel){
    case 0: /* OFF — 타깃 0 (참고: mode==0이면 코어도 정지 상태) */
        for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;
        break;

    case 1: /* L2R 스윕 */
    	Pattern_Breath_Center();
        break;

    case 2: /* 바 — MasterVolume(0..50) */
        LED_Bar_FromMasterVolume();
        break;

    case 3: /* 바 — SoundBalance(0..50) */
        LED_Bar_FromSoundBalance();
        break;

    case 4: /* 브리딩 — 중앙 기준 */
    	//Pattern_MetroBand();
    	Pattern_MetroSingleBeat();
        break;

    case 5: /* 브리딩 — 중앙 기준 */
    	LED_Bar_FromNoteIndex();
        break;
    case 6: /* 브리딩 — 중앙 기준 */
    	Pattern_Tuner_CentTwoLEDs();
        break;

    case 7:
    	LED_Bar_FromMetronomeVolume();
    	break;

    case 8:
    	LED_Pattern_ClipWarn();
    	break;

    default: /* 알 수 없는 모드 → OFF 안전처리 */
        for (int k=0; k<NLED; ++k) s_lin_tgt_q20[k] = 0;
        break;
    }
}




/* ===== 5) 모드 & ISR ====================================================== */
/* LED_ModeSet
 * 내부 러닝 게이트 전용(0/1). 패턴은 바꾸지 않는다.
 * in : 0 = 코어 정지 + 모든 상태/출력 클리어
 *      1 = 코어 실행 (ISR에서 LED_RenderOneTick() 수행됨)
 * 주의: UI/상위 로직은 보통 이 함수를 직접 호출하지 말 것.
 */
void LED_ModeSet(uint8_t mode){
    s_mode = mode;
    if (mode == 0){
        for (int i=0;i<NLED;++i){
            s_pwm_out[i]=0;
            s_out_q16[i]=0;
            s_sd_acc[i]=0;
            s_lin_out_q20[i]=0;
            s_lin_tgt_q20[i]=0;
            led_set(i,0);
        }
    }
}

/* 호환용(120Hz): 실제 로직은 1kHz에서 모두 처리 */
void LED_Tick_120Hz_ISR(void){
    (void)0; // no-op
}

/* 1 kHz 주기: 코어(위치/분배/스무딩/감마) + Q16→ΣΔ→CCR */
void LED_SD_1kHz_ISR(void){
    if (s_mode == 1) {
        LED_Core_1kHz();
    }
    for (int k=0; k<NLED; ++k){
        uint32_t out  = s_out_q16[k];
        uint32_t base = out >> 16;
        uint32_t frac = out & 0xFFFF;

        s_sd_acc[k] += frac;
        uint16_t hw = (uint16_t)base;
        if (s_sd_acc[k] >= 65536u){
            s_sd_acc[k] -= 65536u;
            if (hw < PWM_MAX) hw = (uint16_t)(hw + 1);
        }
        if (hw != s_pwm_out[k]){
            s_pwm_out[k] = hw;
            led_set(k, hw);
        }
    }
}

/* ===== 6) 퍼블릭 설정자 =================================================== */
void LED_SetFlowSpeed(float leds_per_sec){
    if (leds_per_sec < 0.05f) leds_per_sec = 0.05f;
    uint32_t q = (uint32_t)lroundf(leds_per_sec * (float)(1u<<QPOS_SHIFT));
    if (q == 0) q = 1;
    s_step_per_sec_q88 = q;
}
void LED_SetFlowSpread(float leds){
    if (leds < 0.25f) leds = 0.25f;
    s_spread_q8 = (uint16_t)lroundf(leds * 256.f);
}
void LED_SetSmoothingTauMs(uint16_t tau_ms){
    s_tau_ms = (tau_ms < 1) ? 1 : tau_ms;
    BuildSmoothingTable(s_tau_ms);
}

/* ===== 7) PWM 초기화 ====================================================== */
static void PWM_Init_All(void){
    // APB1 타이머들: 84MHz / (1+1) = 42MHz → ARR=41999 → 1kHz
    tim_apply_pwm(&htim3,  1, PWM_ARR);
    tim_apply_pwm(&htim5,  1, PWM_ARR);
    tim_apply_pwm(&htim12, 1, PWM_ARR);
    // APB2 타이머: 168MHz / (3+1) = 42MHz → 1kHz
    tim_apply_pwm(&htim9,  3, PWM_ARR);

    pwm_start(&htim3,  TIM_CHANNEL_3);
    pwm_start(&htim3,  TIM_CHANNEL_4);
    pwm_start(&htim5,  TIM_CHANNEL_1);
    pwm_start(&htim5,  TIM_CHANNEL_3);
    pwm_start(&htim5,  TIM_CHANNEL_4);
    pwm_start(&htim9,  TIM_CHANNEL_1);
    pwm_start(&htim9,  TIM_CHANNEL_2);
    pwm_start(&htim12, TIM_CHANNEL_1);
    pwm_start(&htim12, TIM_CHANNEL_2);
}

/* ===== 8) 초기화 ========================================================== */
void LED_Init(void){
    BuildGammaLUT();               // 감마 LUT
    BuildHannLUT();                // Hann 커널
    BuildEaseLUT_Strong();
    BuildSmoothingTable(s_tau_ms); // dt→k(dt)


    PWM_Init_All();

    memset(s_pwm_out,     0, sizeof(s_pwm_out));
    memset(s_sd_acc,      0, sizeof(s_sd_acc));
    memset(s_out_q16,     0, sizeof(s_out_q16));
    memset(s_lin_out_q20, 0, sizeof(s_lin_out_q20));
    memset(s_lin_tgt_q20, 0, sizeof(s_lin_tgt_q20));

    s_pos_q88 = 0;
    s_last_ms = HAL_GetTick();

    /* 기본 효과 설정(기존 의도 유지) */
    LED_SetFlowSpeed(3.0f);
    LED_SetFlowSpread(1.3f);


    s_met_inited        = 0;
    s_met_beat_start_ms = HAL_GetTick();

}



/* LEDModeSet
 * 외부(UI) 공개 API. 패턴 번호를 설정하고 러닝 게이트를 자동 관리한다.
 * in : mode (0=OFF, 1=L2R 스윕, 2=MasterVolume 바, 3=SoundBalance 바, ...)
 * 효과: g_led_mode_sel <- mode
 *      mode==0 → LED_ModeSet(0)  // 정지 + 모든 출력/상태 클리어
 *      mode!=0 → LED_ModeSet(1)  // 실행
 */
void LEDModeSet(uint8_t mode)
{
    g_led_mode_sel = mode;
    LED_ModeSet( (mode==0) ? 0 : 1 );  // s_mode 게이트는 기존 함수가 처리. :contentReference[oaicite:6]{index=6}
}
