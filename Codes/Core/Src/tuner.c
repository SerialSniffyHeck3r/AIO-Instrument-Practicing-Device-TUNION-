/* ============================================================================
 *  tuner.c
 *
 *  목표:
 *  1) fs 보정 지원 그대로 유지
 *  2) AMDF 기반 기본 주파수
 *  3) 저역일 때 긴 창으로 한번 더 보기
 *  4) 하모닉/옥타브 정정 한 겹 (특히 f/2 내려찍기)
 *  5) 시간영역 결과를 아주 좁은 주파수영역 스캔으로 한 번 더 확인
 *  6) 노트/센트 기반 pre-lock (3프레임이 같은 노트/센트 범위에 있을 때만 잠금)
 *  7) 잠금 이후에만 온건 PLL
 *
 *  이렇게 하면 “사인파는 거의 항상 같은 음으로 잠그고”
 *  “기타 저현도 f/2 정정이 들어가서 엉뚱한 옥타브로 덜 가게” 된다.
 * ============================================================================ */

#include <stdint.h>
#include <string.h>
#include <math.h>
#include "stm32f4xx_hal.h"

#ifndef ARM_MATH_CM4
#  define ARM_MATH_CM4
#endif
#include "arm_math.h"

#if defined(__CCM_RAM)
  #define CCMRAM __attribute__((section(".ccmram")))
#else
  #define CCMRAM
#endif

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/* main.c 에서 실제 값이 있음 */
extern volatile float TunerMeasurement;

/* 여기서 실제로 정의되는 값들 */
volatile int32_t TunerMeasurement_x10 = 0;
volatile int16_t TunerCents_x10       = 0;
volatile uint8_t g_tnr_overload       = 0;

/* 0 = 크로매틱, 1 = 고정 음 튜닝 */
static volatile uint8_t s_tuner_mode = 0;
static float            s_fixed_note_hz = 440.0f;   // 일단 A4 고정


/* 내부 플래그 */
static volatile uint8_t s_tuner_run_flag = 0;

/* ===== 설정 ===== */
#define RING_N              4096u

#define WIN_N               2048   /* 기본 창 */
#define WIN_LOW_N           2048u   /* 저역용 창 */

#define FMIN_HZ             82.0f   /* 기타 6번줄 E2 근처 */
#define FMAX_HZ             1800.0f

/* AMDF 2단계 */
#define AMDF_COARSE_STEP    4u
#define AMDF_FINE_RADIUS    6u

#define GATE_DBFS           (-35.0f)
#define PUB_INTERVAL_MS     5u

#define OVERLOAD_HOLD_MS    200u

/* 잠금 관련 (note 기반으로 바꿀 거지만 기본값은 둠) */
#define BAD_FRAME_MAX       3
#define LOCK_MAX_JUMP_HZ    35.0f
#define LOCK_TRACK_ALPHA    0.15f

// AMDF가 찍어주는 Q가 실제 악기에서 이만큼 안 나오는 것 같으니까 조금 낮춰준다
#define FRONT_MIN_Q         0.08f

/* pre-lock 관련 */
#define PRELOCK_HIST_N          3       /* 3프레임 모아본다 */
#define PRELOCK_MAX_SPAN_CENTS         50.0f   /* 세 프레임이 이 안에 있어야 같은 음으로 본다 */
#define PRELOCK_MIN_Q           0.20f   /* 세 프레임 모두 이 q 이상이어야 잠금 */

/* PLL 관련 */
#define PLL_ENABLE_HZ       180.0f
#define PLL_PHASE_ALPHA     0.35f
#define PLL_FREQ_ALPHA      0.10f
#define PLL_MIN_DT_S        0.001f
#define PLL_FREQ_GUARD_HZ   40.0f

/* 노트 변환용 */
#define NOTE_A4_FREQ    440.0f
#define NOTE_A4_MIDI    69

/* 전역 상태 */
static uint32_t s_ovl_until = 0;
static uint32_t s_fs        = 48000u;
static int32_t  s_fs_ppm    = 0;
static uint8_t  s_enabled   = 0;

CCMRAM static float s_ring[RING_N];
CCMRAM static float s_win_1024[WIN_N];
CCMRAM static float s_win_2048[WIN_LOW_N];
CCMRAM static float s_frame[WIN_LOW_N];

static uint32_t s_wpos         = 0;
static uint32_t s_last_loud_ms = 0;
static uint32_t s_last_pub_ms  = 0;

/* 화면 EMA */
static uint8_t  s_have_ema     = 0;
static float    s_f_ema        = 0.0f;

/* 락 상태 */
typedef enum {
    TLOCK_SEARCH = 0,
    TLOCK_PRELOCK,
    TLOCK_LOCKED
} tuner_lock_state_t;

static tuner_lock_state_t s_lock_state   = TLOCK_SEARCH;
static float              s_lock_hz      = 0.0f;
static uint8_t            s_bad_frames   = 0;

/* pre-lock 버퍼: 프레임별로 노트/센트까지 저장 */
typedef struct {
    float hz;
    float q;
    int   midi;
    float cents;
} prelock_item_t;

static prelock_item_t s_pre[PRELOCK_HIST_N];
static uint8_t        s_pre_cnt = 0;

/* PLL 상태 */
static uint8_t  s_pll_on       = 0;
static float    s_pll_freq_hz  = 0.0f;
static float    s_pll_phase    = 0.0f;
static uint32_t s_pll_last_ms  = 0;

/* ── 유틸 ── */
static inline uint32_t ms_now(void){ return HAL_GetTick(); }
static inline uint32_t ms_since(uint32_t t0){ return HAL_GetTick() - t0; }
static inline float fs_eff(void){
    return (float)s_fs * (1.0f + (float)s_fs_ppm * 1e-6f);
}
static inline float dbfs_from_rms(float r){
    if (r < 1e-12f) r = 1e-12f;
    return 20.0f * log10f(r);
}
static inline void publish_hz(float f){
    int32_t q = (int32_t)lrintf(f * 10.f);
    if (q < 0) q = 0;
    __DMB();
    TunerMeasurement_x10 = q;
    TunerMeasurement     = (float)q * 0.1f;
    __DMB();
}
static inline void publish_cent(float c){
    int16_t q = (int16_t)lrintf(c * 10.f);
    __DMB();
    TunerCents_x10 = q;
    __DMB();
}
static inline float wrap_pm_pi(float x){
    while (x >  PI) x -= 2.f * PI;
    while (x < -PI) x += 2.f * PI;
    return x;
}

/* ── 노트 변환 ── */
static void freq_to_midi_and_cents(float f, int *out_midi, float *out_cents, float *out_note_hz)
{
    if (f <= 0.f) {
        if (out_midi)  *out_midi  = -1;
        if (out_cents) *out_cents = 0.f;
        if (out_note_hz) *out_note_hz = 0.f;
        return;
    }
    float midi_f = (float)NOTE_A4_MIDI + 12.f * (logf(f / NOTE_A4_FREQ) / logf(2.0f));
    int   midi_i = (int)lrintf(midi_f);
    if (midi_i < 0) midi_i = 0;
    if (midi_i > 127) midi_i = 127;
    float note_hz = NOTE_A4_FREQ * powf(2.0f, ((float)midi_i - (float)NOTE_A4_MIDI)/12.f);
    float cents   = 1200.f * (logf(f / note_hz) / logf(2.0f));
    if (out_midi)     *out_midi     = midi_i;
    if (out_cents)    *out_cents    = cents;
    if (out_note_hz)  *out_note_hz  = note_hz;
}

/* ── 윈도 생성 ── */
static void make_hann(void){
    for (uint32_t i=0; i<WIN_N; i++){
        s_win_1024[i] = 0.5f * (1.0f - arm_cos_f32(2.f * PI * (float)i / (float)(WIN_N - 1u)));
    }
    for (uint32_t i=0; i<WIN_LOW_N; i++){
        s_win_2048[i] = 0.5f * (1.0f - arm_cos_f32(2.f * PI * (float)i / (float)(WIN_LOW_N - 1u)));
    }
}

/* ── 링에서 N 샘플 채집 ── */
static float take_window_N(float *dst, uint32_t N, const float *win){
    float acc = 0.f;
    for (uint32_t i=0; i<N; i++){
        uint32_t idx = (s_wpos + RING_N - N + i) % RING_N;
        acc += s_ring[idx];
    }
    float mean = acc / (float)N;
    float acc2 = 0.f;
    for (uint32_t i=0; i<N; i++){
        uint32_t idx = (s_wpos + RING_N - N + i) % RING_N;
        float v = (s_ring[idx] - mean) * win[i];
        dst[i]  = v;
        acc2   += v*v;
    }
    float rms = sqrtf(acc2 / (float)N);
    return dbfs_from_rms(rms);
}

/* ── AMDF 2단계 ── */
static float amdf_pick(const float *x, uint32_t N, float fs, float *out_q)
{
    uint32_t tau_min = (uint32_t)(fs / FMAX_HZ);
    uint32_t tau_max = (uint32_t)(fs / FMIN_HZ);
    if (tau_max >= N) tau_max = N - 1u;

    float    best_score  = 1e30f;
    uint32_t best_tau_i  = 0u;

    for (uint32_t tau = tau_min; tau <= tau_max; tau += AMDF_COARSE_STEP){
        float sum = 0.f;
        uint32_t n2 = N - tau;
        for (uint32_t i=0; i<n2; i++){
            float d = x[i] - x[i+tau];
            sum += (d >= 0.f) ? d : -d;
        }
        float s = sum / (float)n2;
        if (s < best_score){
            best_score = s;
            best_tau_i = tau;
        }
    }

    if (best_tau_i == 0u){
        if (out_q) *out_q = 0.f;
        return 0.f;
    }

    uint32_t fstart = (best_tau_i > AMDF_FINE_RADIUS) ? (best_tau_i - AMDF_FINE_RADIUS) : tau_min;
    uint32_t fend   = best_tau_i + AMDF_FINE_RADIUS;
    if (fend > tau_max) fend = tau_max;

    float    fine_best_score = 1e30f;
    uint32_t fine_best_tau   = best_tau_i;
    for (uint32_t tau = fstart; tau <= fend; tau++){
        float sum = 0.f;
        uint32_t n2 = N - tau;
        for (uint32_t i=0; i<n2; i++){
            float d = x[i] - x[i+tau];
            sum += (d >= 0.f) ? d : -d;
        }
        float s = sum / (float)n2;
        if (s < fine_best_score){
            fine_best_score = s;
            fine_best_tau   = tau;
        }
    }

    if (out_q){
        float q = 1.0f / (1.0f + fine_best_score);
        *out_q = q;
    }

    return fs / (float)fine_best_tau;
}

/* Goertzel magnitude */
static float goertzel_mag2(const float *x, uint32_t N, float fs, float f){
    float w  = 2.f * PI * f / fs;
    float cw = arm_cos_f32(w);
    float sw = arm_sin_f32(w);
    float coeff = 2.f * cw;
    float s0=0.f,s1=0.f,s2=0.f;
    for (uint32_t n=0;n<N;n++){
        s0 = x[n] + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    float re = s1 - s2 * cw;
    float im = s2 * sw;
    return re*re + im*im;
}

/* Goertzel 3점 보간 */
static float refine_goertzel_3pt(const float *x, uint32_t N, float fs, float f0){
    if (f0 <= 0.f) return 0.f;
    float df = fs / (float)N;
    float f1 = f0 - df;
    float f2 = f0 + df;
    if (f1 < 0.f) f1 = f0;
    float m0 = goertzel_mag2(x, N, fs, f0);
    float m1 = goertzel_mag2(x, N, fs, f1);
    float m2 = goertzel_mag2(x, N, fs, f2);
    float denom = (m1 - 2.f*m0 + m2);
    float delta = 0.f;
    if (fabsf(denom) > 1e-12f){
        delta = 0.5f * (m1 - m2) / denom;
    }
    return f0 + delta * df;
}

/* PLL용 IQ */
static void goertzel_iq(const float *x, uint32_t N, float fs, float f, float *out_re, float *out_im)
{
    float w  = 2.f * PI * f / fs;
    float cw = arm_cos_f32(w);
    float sw = arm_sin_f32(w);
    float coeff = 2.f * cw;
    float s0=0.f,s1=0.f,s2=0.f;
    for (uint32_t n=0;n<N;n++){
        s0 = x[n] + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    *out_re = s1 - s2 * cw;
    *out_im = s2 * sw;
}

/* ── 샘플 공급 ── */
extern uint32_t MicInputMode;
void Tuner_FeedInterleavedI2S24(const uint16_t *rx_u16, uint32_t n_u16)
{
    if (!s_enabled) {
        return;
    }

    uint32_t frames = n_u16 / 4u;      // u16 네 개 = L/R 24bit
    uint32_t now    = HAL_GetTick();
    float    local_peak = 0.0f;

    for (uint32_t i = 0; i < frames; i++) {
        int32_t l = ((int32_t)(int16_t)rx_u16[4*i + 0] << 16) | (int32_t)rx_u16[4*i + 1];
        int32_t r = ((int32_t)(int16_t)rx_u16[4*i + 2] << 16) | (int32_t)rx_u16[4*i + 3];

        float fl = (float)l * (1.0f / 2147483648.0f);
        float fr = (float)r * (1.0f / 2147483648.0f);


        float x;
        switch (MicInputMode & 3u) {
          case 1: x = fl; break;   // L-EXP
          case 2: x = fr; break;   // R-EXP
          default: x = 0.5f*(fl+fr); break; // STEREO
        }


        if (x >  1.05f) x =  1.05f;
        if (x < -1.05f) x = -1.05f;

        s_ring[s_wpos] = x;
        s_wpos = (s_wpos + 1u) % RING_N;

        float a = (x >= 0.0f) ? x : -x;
        if (a > local_peak) {
            local_peak = a;
        }
    }

    // 여기 수치만 올렸다. 노이즈를 '있다'고 안 치게.
    if (local_peak > 0.030f) {
        s_last_loud_ms = now;
    }

    if (local_peak >= 0.995f) {
        g_tnr_overload = 1;
        s_ovl_until = now + OVERLOAD_HOLD_MS;
    } else if (g_tnr_overload && (int32_t)(now - s_ovl_until) > 0) {
        g_tnr_overload = 0;
    }
}


/* ISR → 10ms마다 불림 */
/* ISR → 10ms마다 불림: 5번에 한 번만 메인 태스크 실행 (20Hz) */
void Tuner_Tick_10ms_fromISR(void)
{
    static uint8_t s_tick_div = 0;

    if (!s_enabled) return;

    s_tick_div++;
    if (s_tick_div >= 5) {     // 10ms * 5 = 50ms → 20Hz
        s_tick_div = 0;
        s_tuner_run_flag = 1;
    }
}

/* ── 본체 ── */
void Tuner_Task(void)
{
    /* 50ms마다 ISR이 올려준 플래그 처리 */
    if (!s_tuner_run_flag) return;
    s_tuner_run_flag = 0;

    /* 아예 꺼져 있으면 클리어만 */
    if (!s_enabled) {
        publish_hz(0.f);
        publish_cent(0.f);
        return;
    }

    uint32_t now = ms_now();

    /* 200ms 동안 소리 없으면 리셋 */
    if (ms_since(s_last_loud_ms) > 400u) {
        publish_hz(0.f);
        publish_cent(0.f);
        return;
    }

    /* 1) 1024 샘플 창 뽑기 */
    float db = take_window_N(s_frame, WIN_N, s_win_1024);
    if (db < GATE_DBFS) {
        /* 노이즈만 있으면 그냥 0 */
        publish_hz(0.f);
        publish_cent(0.f);
        /* 무음 타이머도 살짝 밀어둠 */
        if (now > 201u) s_last_loud_ms = now - 201u; else s_last_loud_ms = 0;
        return;
    }

    /* 실제 샘플레이트 (49.142857kHz 같은 실제 값이 들어가도록) */
    float fs = fs_eff();

    /* 2) 한 번만 AMDF 돌려서 f0 후보 뽑기 */
    float q_1024 = 0.f;
    float f0     = amdf_pick(s_frame, WIN_N, fs, &q_1024);

    /* 유효 범위 밖이면 버림 */
    if (f0 < FMIN_HZ || f0 > FMAX_HZ || q_1024 < FRONT_MIN_Q) {
        publish_hz(0.f);
        publish_cent(0.f);
        return;
    }

    /* 3) 모드에 따라 크로매틱 / 고정음 처리 */
    if (s_tuner_mode == 0) {
        /* ── 크로매틱: 기존 NOTE_A4_FREQ=440 기준으로 노트/센트 뽑기 ── */
        int   midi;
        float cents;
        float note_hz;
        freq_to_midi_and_cents(f0, &midi, &cents, &note_hz);
        /* publish: Hz는 측정값, 센트는 해당 노트 기준 */
        publish_hz(f0);
        publish_cent(cents);
    } else {
        /* ── 고정 음 튜닝: 지정 주파수와의 센트 차이만 ── */
        float cents = 0.f;
        if (s_fixed_note_hz > 0.f) {
            /* 1200*log2(f0/fixed) */
            cents = 1200.0f * (logf(f0 / s_fixed_note_hz) / logf(2.0f));
        }
        publish_hz(f0);
        publish_cent(cents);
    }
}



/* ==== 초기화/설정 ==== */
void Tuner_Init(uint32_t fs_hz)
{
    s_fs      = fs_hz;
    s_fs_ppm  = 0;
    s_enabled = 0;
    s_wpos    = 0;
    s_last_loud_ms = 0;
    s_have_ema = 0;
    s_lock_state = TLOCK_SEARCH;
    s_pll_on     = 0;
    s_bad_frames = 0;
    s_pre_cnt    = 0;
    make_hann();
    memset(s_ring, 0, sizeof(s_ring));
}

void Tuner_SetEnabled(uint8_t on)
{
    s_enabled = on ? 1u : 0u;
    if (!on){
        publish_hz(0.f);
        publish_cent(0.f);
        s_have_ema   = 0;
        s_lock_state = TLOCK_SEARCH;
        s_pll_on     = 0;
        s_bad_frames = 0;
        s_pre_cnt    = 0;
    }
}

void Tuner_SetFsTrimPpm(int32_t ppm)
{
    if (ppm < -50000) ppm = -50000;
    if (ppm >  50000) ppm =  50000;
    s_fs_ppm = ppm;
}

/* notch 호환 */
void notch_tuner_set_fs_trim_ppm(int32_t ppm){ Tuner_SetFsTrimPpm(ppm); }
void notch_set_tuner_enabled(uint8_t on)     { Tuner_SetEnabled(on); }
