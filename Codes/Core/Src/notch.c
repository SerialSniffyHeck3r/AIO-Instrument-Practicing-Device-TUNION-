///=======================================================================================
// [1] INCLUDES & MACROS
///=======================================================================================

#include "main.h"
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "tuner.h"   // 파일 상단 적당한 위치에 추가되어 있어야 함

// IRQ 스택 보호: 8바이트 정렬 정적 버퍼
static float s_tmpL[512] __attribute__((aligned(8)));
static float s_tmpR[512] __attribute__((aligned(8)));



////테스트 모드에서만 사용하는 함수입니다.
////=========================
/* ==== [NOTE SUSTAIN SFX] — fixed-length beep with zero RAM tables ==== */
static volatile uint8_t  s_note_active   = 0u;
static volatile uint32_t s_note_remain   = 0u;     // 남은 샘플 수
static float             s_note_a        = 0.0f;   // 2*cos(w)
static float             s_note_y1       = 0.0f;   // state y[n-1]
static float             s_note_y2       = 0.0f;   // state y[n-2]
static float             s_note_gain     = 0.5f;   // 내부 게인
static float             s_note_fs       = 48000.0f;

static inline void note_set_fs(float fs){ if (fs >= 8000.0f) s_note_fs = fs; }

/* 외부에서 호출할 공개 API */
void notch_note_start(float freq_hz, uint16_t ms, float gain)
{
    if (freq_hz < 50.0f)   freq_hz = 50.0f;
    if (freq_hz > 8000.0f) freq_hz = 8000.0f;
    if (gain    < 0.02f)   gain    = 0.02f;
    if (gain    > 1.50f)   gain    = 1.50f;

    const float w = 2.0f * (float)M_PI * (freq_hz / s_note_fs);
    s_note_a    = 2.0f * cosf(w);
    s_note_y2   = 0.0f;
    s_note_y1   = sinf(w);               // 초기화(한 주기 진행된 상태)
    s_note_gain = gain;

    uint32_t total = (uint32_t)((s_note_fs * (float)ms) * (1.0f/1000.0f) + 0.5f);
    if (total < 32u) total = 32u;        // 최소 길이
    s_note_remain = total;
    s_note_active = 1u;
}

uint8_t notch_note_busy(void) { return s_note_active; }
void    notch_note_stop(void) { s_note_active = 0u; }






// --- Local helper: avoid name clash with any CMSIS 'q31_to_float' symbol ---
static inline float notch_q31_to_f32(int32_t s) { return (float)s * (1.0f/2147483648.0f); }

/* === Q31 helper forward + legacy alias === */
static inline int32_t notch_float_to_q31_sat(float x);  // forward decl (타입체크용)
#define float_to_q31_sat notch_float_to_q31_sat         // 옛 이름 → 새 이름 매핑

#define ARM_MATH_CM4
#include "arm_math.h"   // CMSIS-DSP


// ==== I2S3 RX 24bit 정렬 교정 (RJ ↔ MSB) ====
//  - PMOD I2S2 ADC가 24bit Right-Justified 로 나오는 경우를 교정
//  - 1: RJ24 on 32비트 프레임 (하위 24비트 유효)  /  0: MSB-aligned(Philips)
#ifndef MIC3_RX_IS_RJ24
#define MIC3_RX_IS_RJ24  0   // ← 우선 1로 두고 테스트. 맞으면 이 값 유지!
#endif

static inline int32_t mic3_unpack_u16x2(uint16_t hi, uint16_t lo)
{
    uint32_t w = ((uint32_t)hi << 16) | (uint32_t)lo;
#if MIC3_RX_IS_RJ24
    // Right-Justified 24bit: 부호비트가 bit23. 먼저 sign-extend 24→32,
    // 그다음 Q31 정렬을 위해 8비트 왼쪽으로 정렬.
    if (w & 0x00800000U) w |= 0xFF000000U;   // sign-extend from bit23
    return (int32_t)(w << 8);
#else
    // MSB-aligned(Philips 24bit in 32b frame): 이미 Q31 정렬 상태
    return (int32_t)w;
#endif
}








//////////////////////
// =================== UI부(main.c)에서 제공되는 심볼들 참조(중복 정의 금지) ========================

extern I2S_HandleTypeDef hi2s2;
extern DMA_HandleTypeDef hdma_i2s2_ext_rx;
extern DMA_HandleTypeDef hdma_spi2_tx;


// 오디오 on/off: UI가 토글. 1=출력, 0=무음
extern volatile uint8_t AudioProcessingIsReady;

// === UI부에서 오는 전역들 ===
extern volatile uint8_t AudioProcessingIsReady;

// ★ UI부(main.c)의 마스터 볼륨(0~50). 50=무감쇄, 0=MUTE
extern volatile uint8_t MasterVolume;

// ==== UI에서 오는 노치 제어 ====
extern volatile uint8_t  CutOffOnOff;        // 0=OFF, 1=IIR 노치 ON
extern volatile uint32_t CutOffFreqStart;    // Hz (예: 300)
extern volatile uint32_t CutOffFreqEnd;      // Hz (예: 600)

// ==== UI에서 오는 피치쉬프트 제어 ====
extern volatile int8_t  PitchSemitone; // 0..6 (3이 중립: -3..+3 semitone)

// ==== UI SOUNDGEN externs ====
extern volatile uint8_t  IsSoundGenReady;     // 1: 사운드젠 on
extern volatile uint8_t  SoundGenMode;        // 0=sine, 1=square, 2=triangle
extern volatile uint8_t  CurrentNoteIndex;    // UI가 관리
extern volatile uint16_t TunerBaseFreq;       // A 기준 (예: 440)
extern volatile float    SoundFrequencyOutput;// UI가 계산한 주파수(Hz), 있으면 이것 우선

// 현재 DSP 모드 값(숫자)만 읽어온다. (UI부 enum 순서: 0..)
extern uint8_t currentDSPModeState;
#define DSP_STATE_SoundGenerator 4           // UI부 enum 상 'SoundGen' 값과 동일

extern volatile uint16_t TunerBaseFreq;       // 예: 440
extern volatile uint8_t  SoundGenMode;        // 0=sine,1=square,2=triangle (SoundGen과 동치)
extern volatile uint8_t  IsSoundGenReady;     // 1이면 SoundGen(혹은 동작음) 허용

#define BLOCK_SIZE_U16   2048u

static uint16_t rxBuf[BLOCK_SIZE_U16 * 2];  // double buffer (half A + half B)
static uint16_t txBuf[BLOCK_SIZE_U16 * 2];
static volatile uint8_t cb_state = 0;       // 0=idle, 1=half, 2=full


extern volatile float TunerMeasurement;   // main.c에서 UI가 읽어감
// [ADD][extern] UI에서 조정하는 SENS 레벨
extern volatile uint8_t g_tnr_sens;

extern volatile uint16_t MetronomeVolume;



/*
 * 파라미터 손대는 기준

DISP_STAB_CENTS_THRESH=10(≈ ±10 cents): 손가락 떨림·배음 변동에 덜 민감. 더 타이트하게 하려면 8, 더 느슨하게 하려면 12~15.

DISP_STAB_FRAMES_MIN=3: 현재 발행 레이트가 약 30ms 최소 간격이므로(내부 발행가드) 3프레임이면 약 90ms 정도의 정착 시간이 필요. 더 느리게 점등하려면 4~5로. 발행 가드는 여기서 조절 중.

notch
 */


// === 볼륨 LUT(감마) ===  (이미 있으면 재사용)
float     g_vol_lin = 1.0f;
uint8_t   g_prev_vol_step = 0xFF;
float     g_vol_lut[51];

static float    g_met_gain_lin   = 1.0f;
static uint16_t g_prev_met_step  = 0xFFFF;

static inline void met_volume_update_if_needed(void)
{
    extern uint8_t g_vol_lut_ready;
    extern float   g_vol_lut[51];
    if (!g_vol_lut_ready) {
        extern void notch_volume_build_lut(void);
        notch_volume_build_lut();
    }
    uint16_t v = (MetronomeVolume > 50) ? 50 : MetronomeVolume;  // 0..50 가정
    if (v != g_prev_met_step) {
        g_met_gain_lin  = g_vol_lut[v];   // 감마 보정된 선형 게인
        g_prev_met_step = v;
    }
}

// ========================= UI SFX (Rotary & Button) =========================
//  - Master Volume(g_vol_lin) 실시간 반영
//  - DMA 콜백에서 최종 I2S2 TX 버퍼에 포화 가산(24bit)로 섞음
//  - 로터리: 사인 × 지수감쇠
//  - 버튼  : 삼각파(짧은 램프)로 부드럽게
//  - 메뉴 복귀: '낮은 비프' 1회 (3연 시퀀스 완전 제거)
// ============================================================================


// UI가 제공: 0..50(권장)  ※ main.c 등에서 정의한 실제 전역을 참조
extern volatile uint16_t SFXVolume;
static float    g_sfx_gain_lin   = 1.0f;
static uint16_t g_prev_sfx_step  = 0xFFFF;

static inline void sfx_volume_update_if_needed(void)
{
    extern uint8_t g_vol_lut_ready;
    extern float   g_vol_lut[51];
    if (!g_vol_lut_ready) {
        extern void notch_volume_build_lut(void);
        notch_volume_build_lut();
    }
    uint16_t v = (SFXVolume > 50) ? 50 : SFXVolume;   // 0..50 가정(감마 일관)
    if (v != g_prev_sfx_step) {
        g_sfx_gain_lin  = g_vol_lut[v];
        g_prev_sfx_step = v;
    }
}



#ifndef UI_ROT_LEN
  #define UI_ROT_LEN           1000u     // 로터리 클릭 길이(샘플) ≈21ms @48k
#endif
#ifndef UI_BTN_LEN
  #define UI_BTN_LEN            1500u     // 버튼 '삑' 길이(샘플) ≈10ms @48k
#endif
#ifndef UI_FS_DEFAULT
  #define UI_FS_DEFAULT      48000.0f
#endif
#ifndef UI_ROT_TAU_MS
  #define UI_ROT_TAU_MS          5.5f    // 로터리 클릭 지수감쇠(ms)
#endif
#ifndef UI_ROT_GAIN
  #define UI_ROT_GAIN            0.8f
#endif
#ifndef UI_BTN_FREQ
  #define UI_BTN_FREQ         1000.0f    // 버튼 기본 주파수
#endif
#ifndef UI_BTN_GAIN
  #define UI_BTN_GAIN            1.2f
#endif

// 메뉴 복귀: 낮은 비프 1회
#ifndef UI_MODE_RETURN_LOW_BEEP_HZ
  #define UI_MODE_RETURN_LOW_BEEP_HZ  300.0f   // 더 낮게 원하면 262.0f(C4)
#endif
#ifndef UI_MODE_RETURN_BEEP_GAIN
  #define UI_MODE_RETURN_BEEP_GAIN      1.2f
#endif
#ifndef UI_MODE_RETURN_BEEP_LEN
  #define UI_MODE_RETURN_BEEP_LEN     UI_BTN_LEN
#endif








// ---------------- 내부 상태/버퍼 (시퀀스 관련 전부 제거) ----------------
static volatile uint8_t  s_uirot_req_pending = 0u;
static volatile uint8_t  s_uibtn_req_pending = 0u;
static volatile uint8_t  s_uibtn_enable      = 1u;

static float             s_ui_fs      = UI_FS_DEFAULT;
static float             s_ui_rot_tau = UI_ROT_TAU_MS;
static float             s_ui_rot_gain= UI_ROT_GAIN;

static float             s_ui_btn_freq = UI_BTN_FREQ;
static float             s_ui_btn_gain = UI_BTN_GAIN;

static volatile float    g_ui_rot_wave[UI_ROT_LEN];
static volatile float    g_ui_btn_wave[UI_BTN_LEN];

static volatile uint8_t  s_uirot_active = 0u;
static volatile uint16_t s_uirot_pos    = 0u;

static volatile uint8_t  s_uibtn_active = 0u;
static volatile uint16_t s_uibtn_pos    = 0u;
static volatile uint16_t s_uibtn_len    = UI_BTN_LEN;

// 외부 볼륨 LUT 갱신/선형 게인
extern void  notch_volume_update_if_needed(void);
extern float g_vol_lin;

// ---------------- 파형 빌더 ----------------
static inline void ui_build_rotary_wave(float fs_hz, float f_hz, float tau_ms, float gain)
{
    if (fs_hz < 1000.0f) fs_hz = UI_FS_DEFAULT;
    if (f_hz < 50.0f)    f_hz  = 50.0f;
    if (tau_ms < 0.3f)   tau_ms= 0.3f;
    s_ui_fs = fs_hz;

    const float tau_s = tau_ms * 0.001f;
    for (uint32_t n=0; n<UI_ROT_LEN; ++n) {
        float t   = (float)n / fs_hz;
        float env = expf(-t / tau_s);
        g_ui_rot_wave[n] = gain * env * sinf(2.0f * (float)M_PI * f_hz * t);
    }
}

// 버튼/복귀 비프: 삼각파 + 짧은 램프(약 1.5ms)로 부드럽게
static inline void ui_build_button_wave(float fs_hz, float f_hz, float gain)
{
    if (fs_hz < 1000.0f) fs_hz = UI_FS_DEFAULT;
    if (f_hz  <   50.0f) f_hz  = 50.0f;
    s_ui_fs = fs_hz;

    const uint32_t att = (uint32_t)(0.0015f * fs_hz + 0.5f);
    const uint32_t rel = att;

    float phase = 0.0f;                 // [0,1)
    const float d = f_hz / fs_hz;       // 위상 증가량

    for (uint32_t n = 0; n < UI_BTN_LEN; ++n) {
        float env = 1.0f;
        if (n < att) {
            env = (float)n / (float)att;                                   // 선형 attack
        } else if (n + rel >= UI_BTN_LEN) {
            env = (float)(UI_BTN_LEN - 1 - n) / (float)rel;                 // 선형 release
        }
        float tri = 2.0f * fabsf(2.0f * phase - 1.0f) - 1.0f;               // 삼각파
        g_ui_btn_wave[n] = gain * env * tri;

        phase += d;
        if (phase >= 1.0f) phase -= 1.0f;
    }
}

// ---------------- UI가 부르는 API ----------------

// 1) 로터리 클릭
void notch_ui_rotary_click_freq(float freq_hz)
{
    ui_build_rotary_wave(s_ui_fs, freq_hz, s_ui_rot_tau, s_ui_rot_gain);
    s_uirot_pos         = 0u;
    s_uirot_active      = 0u;     // arm은 믹서에서
    s_uirot_req_pending = 1u;     // 다음 오디오 블록 시작에서 안전하게 시작
}
void notch_ui_rotary_set_params(float tau_ms, float gain)
{
    if (tau_ms >= 0.3f && tau_ms <= 30.0f) s_ui_rot_tau  = tau_ms;
    if (gain   >= 0.05f && gain   <= 1.20f) s_ui_rot_gain = gain;
}

// 2) 버튼 '삑' on/off
void notch_ui_button_sfx_enable(uint8_t on) { s_uibtn_enable = (on ? 1u : 0u); }

// 3) 버튼 '삑' 트리거(삼각파)
void notch_ui_button_beep(void)
{
    if (!s_uibtn_enable) return;
    s_uibtn_len = UI_BTN_LEN;
    ui_build_button_wave(s_ui_fs, s_ui_btn_freq, s_ui_btn_gain);
    s_uibtn_pos         = 0u;
    s_uibtn_active      = 0u;
    s_uibtn_req_pending = 1u;   // 다음 블록에서 arm
}
void notch_ui_button_set_params(float freq_hz, float gain)
{
    if (freq_hz >= 100.0f && freq_hz <= 8000.0f) s_ui_btn_freq = freq_hz;
    if (gain    >= 0.05f  && gain    <= 1.20f )  s_ui_btn_gain = gain;
}

// 4) 메뉴 복귀: 낮은 비프 1회 (함수명 그대로 유지)
void notch_ui_mode_return_triple_beep(void)
{
    if (!s_uibtn_enable) return;

    s_uibtn_active      = 0u;
    s_uibtn_req_pending = 0u;

    const float    prev_freq = s_ui_btn_freq;
    const float    prev_gain = s_ui_btn_gain;
    const uint16_t prev_len  = s_uibtn_len;

    s_ui_btn_freq = UI_MODE_RETURN_LOW_BEEP_HZ;
    s_ui_btn_gain = UI_MODE_RETURN_BEEP_GAIN;
    s_uibtn_len   = UI_MODE_RETURN_BEEP_LEN;

    ui_build_button_wave(s_ui_fs, s_ui_btn_freq, s_ui_btn_gain);
    s_uibtn_pos         = 0u;
    s_uibtn_active      = 0u;    // arm은 다음 블록에서
    s_uibtn_req_pending = 1u;

    // 복귀 후 기본 버튼값 원상복구
    s_ui_btn_freq = prev_freq;
    s_ui_btn_gain = prev_gain;
    s_uibtn_len   = prev_len;
}

// ---------------- 24bit 오버레이 믹서 (시퀀서 완전 제거) ---------------
static inline int32_t ui_float_to_q31_sat(float x){
    if (x >  0.999999f) x =  0.999999f;
    if (x < -1.000000f) x = -1.000000f;
    return (int32_t)(x * 2147483647.0f);
}

static void ui_sfx_overlay_mix_to_tx(uint16_t *tx_half_u16, uint32_t n_u16)
{
    const uint32_t frames = n_u16 / 4;
    if (frames == 0) return;

    // 블록 경계에서 안전하게 arm
    if (s_uirot_req_pending) { s_uirot_req_pending = 0u; s_uirot_active = 1u; s_uirot_pos = 0u; }
    if (s_uibtn_req_pending) { s_uibtn_req_pending = 0u; s_uibtn_active = 1u; s_uibtn_pos = 0u; }

    // Master Volume 반영
    sfx_volume_update_if_needed();
    const float sfx = g_sfx_gain_lin;


    if (sfx <= 0.000001f) {
        // 뮤트일 때도 타이밍/포인터는 진행(소리만 0)
        for (uint32_t i=0; i<frames; ++i) {
            if (s_uirot_active) { ++s_uirot_pos; if (s_uirot_pos >= UI_ROT_LEN) s_uirot_active = 0u; }
            if (s_uibtn_active) { ++s_uibtn_pos; if (s_uibtn_pos >= s_uibtn_len) s_uibtn_active = 0u; }
        }
        return;
    }

    for (uint32_t i=0; i<frames; ++i) {
        float addf = 0.0f;

        // 로터리
        if (s_uirot_active && s_uirot_pos < UI_ROT_LEN) {
            addf += g_ui_rot_wave[s_uirot_pos];
            ++s_uirot_pos;
            if (s_uirot_pos >= UI_ROT_LEN) s_uirot_active = 0u;
        }

        // 버튼/복귀 공통
        if (s_uibtn_active && s_uibtn_pos < s_uibtn_len) {
            addf += g_ui_btn_wave[s_uibtn_pos];
            ++s_uibtn_pos;
            if (s_uibtn_pos >= s_uibtn_len) s_uibtn_active = 0u;
        }

        /* === [NEW] NOTE SUSTAIN: 고정길이 사인톤을 연속으로 가산 === */
        if (s_note_active && s_note_remain) {
            // 2nd-order oscillator: y[n] = a*y[n-1] - y[n-2]
            float yn = s_note_a * s_note_y1 - s_note_y2;
            s_note_y2 = s_note_y1;
            s_note_y1 = yn;

            // 짧은 attack/release로 클릭 방지(각각 1.5ms)
            float env = 1.0f;
            const float att = s_note_fs * 0.0015f;
            const float rel = att;
            uint32_t passed = (uint32_t)((s_note_fs * (float)i) / (float)frames); // 대략적 진행량
            if (passed < (uint32_t)att) env = (float)passed / (float)att;
            else if (s_note_remain < (uint32_t)rel) env = (float)(s_note_remain) / (float)rel;

            addf += (s_note_gain * env) * yn;

            if (--s_note_remain == 0u) s_note_active = 0u;
        }


        if (addf == 0.0f) continue;

        //
        addf *= sfx;   // ★ 여기서만 곱한다

        // float → Q31 → 24bit 포화 가산
        int32_t add = ui_float_to_q31_sat(addf);

        // L
        uint32_t wL = ((uint32_t)tx_half_u16[4*i+0] << 16) | tx_half_u16[4*i+1];
        int64_t  sl = (int64_t)(int32_t)wL + (int64_t)add;
        if (sl >  2147483647LL) sl =  2147483647LL;
        if (sl < -2147483648LL) sl = -2147483648LL;
        // R
        uint32_t wR = ((uint32_t)tx_half_u16[4*i+2] << 16) | tx_half_u16[4*i+3];
        int64_t  sr = (int64_t)(int32_t)wR + (int64_t)add;
        if (sr >  2147483647LL) sr =  2147483647LL;
        if (sr < -2147483648LL) sr = -2147483648LL;

        uint32_t ul = (uint32_t)((int32_t)sl);
        uint32_t ur = (uint32_t)((int32_t)sr);
        tx_half_u16[4*i+0] = (uint16_t)(ul >> 16);
        tx_half_u16[4*i+1] = (uint16_t)(ul & 0xFFFF);
        tx_half_u16[4*i+2] = (uint16_t)(ur >> 16);
        tx_half_u16[4*i+3] = (uint16_t)(ur & 0xFFFF);
    }
}
// ======================= End of UI SFX engine ===============================


















/* ==== [METRONOME CLICK SFX] state ======================================= */
#ifndef MET_CLICK_LEN
  #define MET_CLICK_LEN        1024u      // ✅ 배열 크기: 정수 상수
#endif
#ifndef MET_FS_DEFAULT
  #define MET_FS_DEFAULT       48000.0f
#endif
#ifndef MET_CLICK_F_HI
  #define MET_CLICK_F_HI       1600.0f    // 하이 톤(악센트)
#endif
#ifndef MET_CLICK_F_LO
  #define MET_CLICK_F_LO        600.0f    // 로 톤(일반)
#endif
#ifndef MET_CLICK_TAU_MS
  #define MET_CLICK_TAU_MS        20.0f   // 감쇠 상수(ms)
#endif
#ifndef MET_CLICK_HI_GAIN
  #define MET_CLICK_HI_GAIN       0.7f    // 기본 음량(악센트)
#endif
#ifndef MET_CLICK_LO_GAIN
  #define MET_CLICK_LO_GAIN       0.5f    // 기본 음량(일반)
#endif

// 👉 서브비트용 파라미터 추가
#ifndef MET_CLICK_SUBLEN
  #define MET_CLICK_SUBLEN        1024u      // ✅ 배열 크기: 정수 상수
#endif
#ifndef MET_CLICK_SUB_F
  #define MET_CLICK_SUB_F        250.0f   // 서브비트는 좀 더 낮은 톤
#endif
#ifndef MET_CLICK_SUB_GAIN
  #define MET_CLICK_SUB_GAIN       0.2f   // 소리도 작게
#endif



// (안전검사)
#if (MET_CLICK_LEN < 32) || (MET_CLICK_LEN > 4096)
  #error "MET_CLICK_LEN must be between 32 and 4096"
#endif

// ===== 파형 테이블 & 게인 전역 =====
// ⚠️ 배열 크기는 반드시 위 매크로(MET_CLICK_LEN)를 사용해야 함(정수 상수)
static float        g_met_hi_wave[MET_CLICK_LEN];   // 악센트 파형
static float        g_met_lo_wave[MET_CLICK_LEN];   // 일반 파형
static float        g_met_sub_wave[MET_CLICK_SUBLEN];  // 🆕 서브비트 파형

volatile float      g_met_hi_gain  = MET_CLICK_HI_GAIN;    // 런타임 조정 가능
volatile float      g_met_lo_gain  = MET_CLICK_LO_GAIN;
volatile float      g_met_sub_gain = MET_CLICK_SUB_GAIN;

// (선택) 런타임 파라미터(톤/감쇠)를 바꾸고 싶으면 이 변수들만 바꾼 후 테이블 재빌드
static volatile float s_met_f_hi    = MET_CLICK_F_HI;
static volatile float s_met_f_lo    = MET_CLICK_F_LO;
static volatile float s_met_f_sub   = MET_CLICK_SUB_F;     // 🆕 서브비트 톤
static volatile float s_met_tau_ms  = MET_CLICK_TAU_MS;

// (참조용) 현재 샘플레이트
static volatile float s_met_fs      = MET_FS_DEFAULT;

/* 내부 상태 */
static volatile uint8_t  s_met_req_pending = 0;   // UI 트리거가 set → 오디오 블록 시작 시 arm
static volatile uint8_t  s_met_req_accent  = 0;   // 1=악센트(첫 박), 0=보통 박
static volatile uint8_t  s_met_req_sub     = 0;   // 1=서브비트
static volatile uint8_t  s_met_active      = 0;   // 현재 클릭 재생중?
static          uint16_t s_met_pos         = 0;   // wave index
static  const   float   *s_met_wave        = NULL;
static          float    s_met_gain        = 0.0f;

/* UI에서 호출할 트리거 API: 한 박 발생 시 호출 (accent: 1=첫 박) */
void notch_metronome_click(uint8_t accent)
{
    s_met_req_accent  = (accent ? 1u : 0u);
    s_met_req_sub     = 0u;         // 이 호출에서는 서브비트 비우기
    s_met_req_pending = 1u;         // 실제 arm은 오디오 처리 블록에서 안전하게 수행
}

/* 🆕 서브비트 전용 트리거 */
void notch_metronome_subclick(void)
{
    s_met_req_accent  = 0u;
    s_met_req_sub     = 1u;
    s_met_req_pending = 1u;
}

static inline void metronome_set_timbre(float f_hi, float f_lo, float tau_ms)
{
    if (f_hi > 50.f && f_hi < 10000.f)  s_met_f_hi  = f_hi;
    if (f_lo > 50.f && f_lo < 10000.f)  s_met_f_lo  = f_lo;
    if (tau_ms >= 0.3f && tau_ms <= 20.f) s_met_tau_ms = tau_ms;
}

// === REPLACE WHOLE FUNCTION: met_click_build_tables ===
void met_click_build_tables(float fs_hz)
{
    if (fs_hz < 1000.0f) fs_hz = MET_FS_DEFAULT;
    s_met_fs = fs_hz;

    const float f_hi  = s_met_f_hi;
    const float f_lo  = s_met_f_lo;
    const float f_sub = s_met_f_sub;
    const float tau   = s_met_tau_ms * 0.001f;  // ms → s

    for (uint32_t n = 0; n < MET_CLICK_LEN; ++n) {
        float t   = (float)n / fs_hz;
        float env = expf(-t / tau);
        g_met_hi_wave[n]  = env * sinf(2.0f * (float)M_PI * f_hi  * t);
        g_met_lo_wave[n]  = env * sinf(2.0f * (float)M_PI * f_lo  * t);
        g_met_sub_wave[n] = env * sinf(2.0f * (float)M_PI * f_sub * t);
    }
}

/* float L/R 버퍼에 클릭을 섞기: 24bit 패킹 '직전'에 호출할 것 */
// === REPLACE WHOLE FUNCTION: met_click_mix_block ===
static inline void met_click_mix_block(float* __restrict L, float* __restrict R, uint32_t N)
{
    // 1) UI가 트리거해둔 요청이 있으면 이번 블록 시작에서 arm
    if (s_met_req_pending) {
        s_met_req_pending = 0u;
        s_met_active      = 1u;
        s_met_pos         = 0;

        if (s_met_req_accent) {
            s_met_wave = (const float*)g_met_hi_wave;
            s_met_gain = g_met_hi_gain;
        } else if (s_met_req_sub) {
            s_met_wave = (const float*)g_met_sub_wave;
            s_met_gain = g_met_sub_gain;
        } else {
            s_met_wave = (const float*)g_met_lo_wave;
            s_met_gain = g_met_lo_gain;
        }

        // 한 번 arm하면 플래그는 즉시 클리어
        s_met_req_accent = 0u;
        s_met_req_sub    = 0u;
    }

    // 2) active면 남은 길이만큼만 더하고 끝나면 종료
    if (!s_met_active || !s_met_wave) return;

    uint32_t remain = (uint32_t)MET_CLICK_LEN - (uint32_t)s_met_pos;
    uint32_t addN   = (N < remain) ? N : remain;

    // 볼륨 LUT 반영
    met_volume_update_if_needed();
    const float mv = g_met_gain_lin;


    for (uint32_t i = 0; i < addN; ++i) {
        float v = s_met_wave[s_met_pos + i] * s_met_gain * mv;
        L[i] += v;
        R[i] += v;
    }

    s_met_pos += addN;
    if (s_met_pos >= MET_CLICK_LEN) {
        s_met_active = 0u;
    }
}


/* ==== [END METRONOME CLICK SFX] ========================================= */





static inline float unwrap(float d){
    while (d >  PI) d -= 2.f*PI;
    while (d < -PI) d += 2.f*PI;
    return d;
}


static inline float phase_unwrap(float d){
    while (d >  PI) d -= 2.f*PI;
    while (d < -PI) d += 2.f*PI;
    return d;
}




/// MIC 합성용
// [ADD-1] ==== I2S3(마이크) 믹싱용 extern & 전역 ====

// I2S3 핸들(CubeMX에서 생성되어 있음) - 다른 파일 수정 없이 참조만
extern I2S_HandleTypeDef hi2s3;

// UI부에서 쓸 수 있는 마이크 밸런스(0..50). (MasterVolume과 같은 감마 적용)
// main.c에서 이미 정의돼 있다면 extern만 남기고, 없다면 여기서 임시로 유지 가능.
extern volatile uint16_t SoundBalance;

// ==== I2S3 RX용 더블버퍼: I2S2와 동일 half 크기 사용 ====
static uint16_t mic3RxBuf[BLOCK_SIZE_U16 * 2];
static volatile uint8_t mic3_running = 0;
// 현재 DMA half (0=half-A, 1=half-B)를 DSP 블록에 전달
static volatile uint8_t g_dma_half = 0;
static volatile uint8_t g_dma_half_mic3 = 0;

// I2S3에서 온 한 half를 그대로 보관해서 VU/튜너가 같이 쓰게 하는 버퍼
// Philips I2S니까 포맷 변환은 안 하고 memcpy만 한다
// I2S3 DMA에 실제로 건 u16 길이
#define I2S3_DMA_U16    (BLOCK_SIZE_U16)

// DMA가 half-cplt 때 채우는 실제 길이
#define I2S3_HALF_U16   (I2S3_DMA_U16 / 2)

static uint16_t s_i2s3_safe[2][I2S3_DMA_U16];
static uint8_t  s_i2s3_cur = 0;          // 지금 채우는 버퍼 인덱스 (0 or 1)

static inline void mic3_sanitize_half_for_analyzers(uint32_t half_index)
{
    // half_index == 0 → 앞 절반
    // half_index == 1 → DMA가 실제로 채운 뒤 절반
    uint16_t *src;
    if (half_index == 0) {
        src = &mic3RxBuf[0];
    } else {
        src = &mic3RxBuf[I2S3_HALF_U16];
    }

    uint16_t *dst = s_i2s3_safe[half_index];

    // Philips I2S니까 포맷 변환 없이 실제로 채워진 구간만 복사
    memcpy(dst, src, I2S3_HALF_U16 * sizeof(uint16_t));
}


// ==== SoundBalance 감마(=MasterVolume과 동일 LUT 재사용) ====
static float    g_mic_gain_lin = 0.0f;  // SoundBalance로부터 얻는 선형 게인
static uint8_t  g_prev_sb_step  = 0xFF; // 이전 단계 캐시



// I2S3 DMA가 채운 half를 VU/튜너가 먹기 전에
// 프로젝트 내부 포맷(MSB-aligned 24bit → u16 4개)으로 강제로 맞춰주는 함수


// MasterVolume에서 쓰는 g_vol_lut[0..50]와 같은 테이블을 재사용한다.
// (이미 notch_volume_build_lut()/g_vol_lut_ready가 이 파일에 존재)
// === [REPLACE] 마이크 밸런스 갱신: MasterVolume과 완전 분리 ===
//  - SoundBalance(0..50)만으로 g_mic_gain_lin 산출
//  - main.c와 같은 감마 LUT(g_vol_lut) 재사용
static inline void mic_balance_update_if_needed(void)
{
    extern uint8_t  g_vol_lut_ready;
    extern float    g_vol_lut[51];
    if (!g_vol_lut_ready) {
        extern void notch_volume_build_lut(void);
        notch_volume_build_lut();
    }

    static uint8_t g_prev_sb_step = 0xFF;
    uint8_t v = (SoundBalance > 50) ? 50 : (uint8_t)SoundBalance;
    if (v != g_prev_sb_step) {
        g_mic_gain_lin = g_vol_lut[v];   // ★ MasterVolume과 독립
        g_prev_sb_step = v;
    }
}



// === 마이크 프리게인/AGC (UI에서 제어; main.c에서 정의) ===
extern volatile int8_t  MicBoost_dB;  // 예: -12..+12 dB 범위 권장
extern volatile uint8_t MicAGC_On;    // 0=off, 1=on
extern volatile uint16_t MicInputMode; // 0=Stereo, 1=L 확장, 2=R 확장  ← ★ 추가



// 내부 상태(아주 소량만)
static float s_agc_gain = 1.0f;    // 블록 스무딩된 AGC 배율
static inline float db2lin(float dB) { return powf(10.0f, dB/20.0f); }
static inline float clampf(float x, float a, float b){ return (x<a)?a:((x>b)?b:x); }



// ★ 추가: APR=0일 때 현재 half를 마이크만으로 채우는 헬퍼
static inline void render_mic_only_to_tx(uint16_t *tx_half_u16, uint8_t which_half, uint32_t n_u16)
{
    const uint32_t frames = n_u16 / 4u;

    for (uint32_t i=0; i<frames; ++i) { s_tmpL[i] = 0.0f; s_tmpR[i] = 0.0f; }
    mic3_mix_into_float(s_tmpL, s_tmpR, frames, which_half);

    for (uint32_t i=0; i<frames; ++i){
        int32_t qL = notch_float_to_q31_sat(s_tmpL[i]);
        int32_t qR = notch_float_to_q31_sat(s_tmpR[i]);
        tx_half_u16[4*i+0] = (uint16_t)((uint32_t)qL >> 16);
        tx_half_u16[4*i+1] = (uint16_t)((uint32_t)qL & 0xFFFF);
        tx_half_u16[4*i+2] = (uint16_t)((uint32_t)qR >> 16);
        tx_half_u16[4*i+3] = (uint16_t)((uint32_t)qR & 0xFFFF);
    }
}



// --- [NEW] I2S3 MIC mini-VU (0..8) ---


static volatile uint8_t s_vu_mic8 = 0;

// --- [NEW] I2S3 MIC mini-VU (0..30) unified for Mini VU ---
static volatile uint8_t s_vu_mic30 = 0;               // 0..30
void notch_get_mic_vu_segments(uint8_t *seg_out) {    // mono
    if (seg_out) *seg_out = s_vu_mic30;
}

uint8_t notch_get_mic_vu8(void) { return s_vu_mic8; }

#define MIC_VU_GATE_DB   (-55.0f)  // 이보다 작으면 무조건 0칸
#define MIC_VU_FLOOR_DB  (-40.0f)  // 이 지점부터 0..8로 매핑 시작
#define MIC_VU_SPAN_DB     45.0f   // -50..-5dB 정도의 구간을 0..8로

// ISR에서도 가볍게 돌도록 디메이션 + 절대피크 기반
// 공통 VU 계산기: 아무 버퍼나 받아서 VU 계산
static void mic3_update_mini_vu_from_buf(const uint16_t *src16, uint32_t u16_n)
{
    const uint32_t frames = u16_n / 4; // (Lhi,Llo,Rhi,Rlo)
    const uint32_t HOP    = 8;
    int32_t maxabs = 0;

    for (uint32_t i = 0; i < frames; i += HOP) {
        int32_t l = ((int32_t)(int16_t)src16[4*i + 0] << 16) | (int32_t)src16[4*i + 1];
        int32_t r = ((int32_t)(int16_t)src16[4*i + 2] << 16) | (int32_t)src16[4*i + 3];

        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > maxabs) maxabs = l;
        if (r > maxabs) maxabs = r;
    }

    const float peak = (float)maxabs * (1.0f / 2147483648.0f);
    if (peak <= 1e-8f) {
        s_vu_mic8  = 0;
        s_vu_mic30 = 0;
        return;
    }

    const float db = 8.685889638f * logf(peak);

    {   // 0..30칸
        const float FLOOR_DB = -60.0f;
        float db_c = (db < FLOOR_DB) ? FLOOR_DB : db;
        float frac = (db_c - FLOOR_DB) / (-FLOOR_DB);
        if (frac < 0.f) frac = 0.f;
        if (frac > 1.f) frac = 1.f;
        s_vu_mic30 = (uint8_t)(frac * 30.0f + 0.5f);
    }

    if (db < MIC_VU_GATE_DB) {
        s_vu_mic8 = 0;
        return;
    }

    float t = (db - MIC_VU_FLOOR_DB) / MIC_VU_SPAN_DB;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    uint8_t seg = (uint8_t)(t * 8.999f);
    s_vu_mic8 = seg;
}

// 기존 시그니처도 그대로 유지
void mic3_update_mini_vu_from_half(uint32_t half_index)
{
    const uint16_t *src16 = &mic3RxBuf[(half_index ? BLOCK_SIZE_U16 : 0)];
    mic3_update_mini_vu_from_buf(src16, BLOCK_SIZE_U16);
}


// //////////////////// DSP 관련

#define DSP_MAX_NOTCH_SECTS   5
#define DSP_NOTCH_Q_MIN       0.5f
#define DSP_NOTCH_Q_MAX       80.f
#define DSP_NOTCH_OVERLAP     1.25f
#ifndef DSP_NOTCH_EDGE_BOOST
#define DSP_NOTCH_EDGE_BOOST  1
#endif

// == Bank-A ==
arm_biquad_cascade_df2T_instance_f32 g_notchA_L, g_notchA_R;
float g_notchA_coeffs[5*DSP_MAX_NOTCH_SECTS];
float g_notchA_stateL[2*DSP_MAX_NOTCH_SECTS];
float g_notchA_stateR[2*DSP_MAX_NOTCH_SECTS];
uint32_t g_notchA_stages = 0;

// == Bank-B (stagger) ==
arm_biquad_cascade_df2T_instance_f32 g_notchB_L, g_notchB_R;
float g_notchB_coeffs[5*DSP_MAX_NOTCH_SECTS];
float g_notchB_stateL[2*DSP_MAX_NOTCH_SECTS];
float g_notchB_stateR[2*DSP_MAX_NOTCH_SECTS];
uint32_t g_notchB_stages = 0;

// 샘플레이트/파라미터 캐시
uint32_t g_fs_hz = 48000;
uint8_t  g_prev_on  = 0xFF;
uint32_t g_prev_fS  = 0xFFFFFFFFu;
uint32_t g_prev_fE  = 0xFFFFFFFFu;



uint8_t   g_vol_lut_ready = 0;

// biquad(2차) 1스테이지 노치: 좌/우 별도 상태, 계수는 공유
// 블록처리용 버퍼 (half-buffer = 2048 halfwords = 512 stereo frames)
static float s_bufL[512];
static float s_bufR[512];





// 스테이지 수 추정
static inline uint32_t notch_choose_stages(float bw_hz){
  if (bw_hz < 80.f)   return 1u;
  if (bw_hz < 300.f)  return 3u;
  if (bw_hz < 1200.f) return 4u;
  return 5u;
}

// DF2T 계수 1섹션 생성: RBJ Notch, a0=1 정규화, {b0,b1,b2,-a1,-a2}
static inline void make_notch_df2t(float fs, float fc, float Q, float *c5)
{
  if (Q < DSP_NOTCH_Q_MIN) Q = DSP_NOTCH_Q_MIN;
  if (Q > DSP_NOTCH_Q_MAX) Q = DSP_NOTCH_Q_MAX;

  float w0 = 2.f * (float)PI * (fc/fs);
  float cw = arm_cos_f32(w0);
  float sw = arm_sin_f32(w0);
  float alpha = sw/(2.f*Q);

  float b0 = 1.f,     b1 = -2.f*cw,    b2 = 1.f;
  float a0 = 1.f+alpha, a1 = -2.f*cw,  a2 = 1.f-alpha;

  float b0n=b0/a0, b1n=b1/a0, b2n=b2/a0;
  float a1n=a1/a0, a2n=a2/a0;

  c5[0]=b0n; c5[1]=b1n; c5[2]=b2n;
  c5[3]=-a1n; c5[4]=-a2n;  // ★ CMSIS DF2T 포맷: -a1, -a2 !!
}

// DSP부(main.c)와 동일 구조의 멀티스테이지 노치 설계
void dsp_style_notch_design_from_ui(void)
{
  float fs = (float)g_fs_hz;
  float f1 = (float)CutOffFreqStart;
  float f2 = (float)CutOffFreqEnd;

  if (f1 < 20.f) f1 = 20.f;
  if (f2 > fs*0.5f - 100.f) f2 = fs*0.5f - 100.f;
  if (f2 <= f1 + 1.f) f2 = f1 + 1.f;

  float bw = f2 - f1;
  float ratio = f2 / f1;

  // ----- Bank-A -----
  uint32_t nA = notch_choose_stages(bw);
  if (nA > DSP_MAX_NOTCH_SECTS) nA = DSP_MAX_NOTCH_SECTS;
  g_notchA_stages = nA;

  float rstep = (nA>1) ? powf(ratio, 1.f/(float)(nA-1)) : 1.f;
  float local_bw = (bw/(float)nA) * DSP_NOTCH_OVERLAP;

  memset(g_notchA_stateL, 0, sizeof(g_notchA_stateL));
  memset(g_notchA_stateR, 0, sizeof(g_notchA_stateR));

  for (uint32_t k=0; k<nA; ++k){
    float fc = (nA==1)? 0.5f*(f1+f2) : f1 * powf(rstep, (float)k);
    float Q  = fc / local_bw;
    make_notch_df2t(fs, fc, Q, &g_notchA_coeffs[5*k]);
  }

  arm_biquad_cascade_df2T_init_f32(&g_notchA_L, nA, g_notchA_coeffs, g_notchA_stateL);
  arm_biquad_cascade_df2T_init_f32(&g_notchA_R, nA, g_notchA_coeffs, g_notchA_stateR);

  // ----- Bank-B (stagger) -----
  uint32_t nB = nA;
  g_notchB_stages = nB;
  memset(g_notchB_stateL, 0, sizeof(g_notchB_stateL));
  memset(g_notchB_stateR, 0, sizeof(g_notchB_stateR));

  for (uint32_t k=0; k<nB; ++k){
    float fc = (nB==1)? 0.5f*(f1+f2) : f1 * powf(ratio, ( (float)k + 0.5f )/ (float)(nB-1) );
    if (fc < f1) fc = f1; if (fc > f2) fc = f2;
    float Q  = fc / (local_bw * 1.10f);
    make_notch_df2t(fs, fc, Q, &g_notchB_coeffs[5*k]);
  }

#if DSP_NOTCH_EDGE_BOOST
  // Start/End 얇은 노치 보강(가능할 때만)
  if (g_notchB_stages < DSP_MAX_NOTCH_SECTS){
    float fc = f1 * 1.05f; if (fc > f2) fc = f2;
    float Q  = fmaxf(3.0f, fc/(bw*0.25f));
    make_notch_df2t(fs, fc, Q, &g_notchB_coeffs[5*g_notchB_stages++]);
  }
  if (g_notchB_stages < DSP_MAX_NOTCH_SECTS){
    float fc = f2 * 0.95f; if (fc < f1) fc = f1;
    float Q  = fmaxf(3.0f, fc/(bw*0.25f));
    make_notch_df2t(fs, fc, Q, &g_notchB_coeffs[5*g_notchB_stages++]);
  }
#endif

  arm_biquad_cascade_df2T_init_f32(&g_notchB_L, g_notchB_stages, g_notchB_coeffs, g_notchB_stateL);
  arm_biquad_cascade_df2T_init_f32(&g_notchB_R, g_notchB_stages, g_notchB_coeffs, g_notchB_stateR);
}














///=======================================================================================
///=======================================================================================
///=======================================================================================

///=======================================================================================
///=======================================================================================
///=======================================================================================





///=======================================================================================
// ===================== DIGITAL VOLUME (0~50, 감마 곡선 동일 적용) =====================
///=======================================================================================

// main.c의 볼륨 곡선과 ‘완전히 동일’하게 맞춤 (감마법 사용)
//   - 50 → 1.0 (0 dB), 0 → 0.0 (MUTE)
#define DSP_VOL_LAW_DB            0
#define DSP_VOL_LAW_EQUAL_POWER   1
#define DSP_VOL_LAW_GAMMA         2

// ★요구사항: “감마 곡선” 동일 적용
#ifndef DSP_VOL_LAW
  #define DSP_VOL_LAW DSP_VOL_LAW_GAMMA
#endif

#ifndef DSP_VOL_MIN_DB
  #define DSP_VOL_MIN_DB  (-60.0f)
#endif
#ifndef DSP_VOL_GAMMA
  #define DSP_VOL_GAMMA   (1.6f)   // main.c의 감마값과 동일
#endif
#ifndef DSP_VOL_EQPOW_GAMMA
  #define DSP_VOL_EQPOW_GAMMA (1.10f)
#endif


// ==== [ADD] 볼륨 LUT (이미 같은 함수가 있으면 그걸 사용) ====
void notch_volume_build_lut(void) {
  for (int v = 0; v <= 50; ++v) {
    float g = 0.0f;
    if (v == 0) g = 0.0f;
    else {
      float t = (float)v / 50.0f;
      // 감마 1.6 고정 (UI부와 동일)
      float a = powf(t, 1.6f);
      // -60 dB 바닥 → 0..1 스케일, 50=1.0(0dB)
      // (UI부와 동일하게 맞춤: 이미 너가 쓰던 방식 유지)
      g = a;
    }
    g_vol_lut[v] = g;
  }
  g_vol_lut_ready = 1;
}

inline void notch_volume_update_if_needed(void) {
  if (!g_vol_lut_ready) notch_volume_build_lut();
  uint8_t v = MasterVolume;
  if (v > 50) v = 50;
  if (v != g_prev_vol_step) {
    g_vol_lin = g_vol_lut[v];
    g_prev_vol_step = v;
  }
}

static inline void notch_volume_update(void)
{
  if (!g_vol_lut_ready) notch_volume_build_lut();
  uint8_t v = MasterVolume;         // 0..50
  if (v > 50) v = 50;
  if (v != g_prev_vol_step) {
    g_vol_lin = g_vol_lut[v];
    g_prev_vol_step = v;
  }
}

// ==== [REPLACE] 패스스루(+볼륨) ====
static inline void notch_copy_with_volume(const uint16_t *src16, uint16_t *dst16, uint32_t n_u16)
{
  notch_volume_update_if_needed();

  const uint32_t frames = n_u16 / 4; // 24bit L/R = 32bit + 32bit = u16 4개
  const float g = g_vol_lin;
  for (uint32_t i = 0; i < frames; ++i) {
    // 24-bit MSB aligned -> 32bit 취급
    uint32_t wL = ((uint32_t)src16[4*i+0] << 16) | src16[4*i+1];
    uint32_t wR = ((uint32_t)src16[4*i+2] << 16) | src16[4*i+3];
    int32_t  sL = (int32_t)wL; // 상위 24bit에 부호 포함
    int32_t  sR = (int32_t)wR;

    // 볼륨
    float xL = (float)sL * (1.0f/2147483648.0f);
    float xR = (float)sR * (1.0f/2147483648.0f);
    xL *= g; xR *= g;

    // back to 24bit MSB aligned
    int32_t yL = (int32_t)(xL * 2147483647.0f);
    int32_t yR = (int32_t)(xR * 2147483647.0f);

    dst16[4*i+0] = (uint16_t)((uint32_t)yL >> 16);
    dst16[4*i+1] = (uint16_t)((uint32_t)yL & 0xFFFF);
    dst16[4*i+2] = (uint16_t)((uint32_t)yR >> 16);
    dst16[4*i+3] = (uint16_t)((uint32_t)yR & 0xFFFF);
  }
}

// [ADD-2] ==== 24bit 유틸 & 마이크 오버레이 믹서 ====
// === METRONOME: overlay click (mono) into packed I2S2 TX buffer ===
// === REPLACE WHOLE FUNCTION: met_click_overlay_mix_to_tx ===
//  - tx_half_u16 : I2S2 TX 반버퍼(u16*), 24bit MSB-aligned stereo (Lhi,Llo,Rhi,Rlo)
//  - n_u16       : 반버퍼 u16 개수(= BLOCK_SIZE_U16)
static inline void met_click_overlay_mix_to_tx(uint16_t *tx_half_u16, uint32_t n_u16)
{
    // 1) UI가 예약해 둔 클릭이 있으면 이번 반버퍼 시작에서 안전하게 arm
    if (s_met_req_pending) {
        s_met_req_pending = 0u;
        s_met_active = 1u;
        s_met_pos    = 0;
        if (s_met_req_accent) {
            s_met_wave = (const float*)g_met_hi_wave;
            s_met_gain = g_met_hi_gain;
        } else {
            s_met_wave = (const float*)g_met_lo_wave;
            s_met_gain = g_met_lo_gain;
        }
    }

    if (!s_met_active || !s_met_wave) return;

    // ★ Master Volume(= g_vol_lin) 최신값 가져오기
    notch_volume_update_if_needed();
    const float mv = g_vol_lin;               // 0.0~1.0
    if (mv <= 0.000001f) {
        // 완전 뮤트면 클릭도 얌전히 패스
        return;
    }

    const uint32_t frames = n_u16 / 4u;       // 1 frame = Lhi,Llo,Rhi,Rlo
    for (uint32_t i = 0; i < frames; ++i) {
        if (s_met_pos >= MET_CLICK_LEN) { s_met_active = 0; break; }

        // mono click → 양 채널 동일 가산 (+ Master Volume 적용)
        float   v   = s_met_wave[s_met_pos] * s_met_gain * mv;  // ★ MV 적용점
        int32_t add = float_to_q31_sat(v);                      // 정수화(포화)

        // 현재 TX 값 읽기 (24bit MSB aligned → 32bit 부호 확장)
        uint32_t wL = ((uint32_t)tx_half_u16[4*i+0] << 16) | tx_half_u16[4*i+1];
        uint32_t wR = ((uint32_t)tx_half_u16[4*i+2] << 16) | tx_half_u16[4*i+3];
        int32_t  sL = (int32_t)wL;
        int32_t  sR = (int32_t)wR;

        // 포화 가산
        int64_t sl = (int64_t)sL + (int64_t)add;
        int64_t sr = (int64_t)sR + (int64_t)add;
        if (sl >  2147483647LL) sl =  2147483647LL;
        if (sl < -2147483648LL) sl = -2147483648LL;
        if (sr >  2147483647LL) sr =  2147483647LL;
        if (sr < -2147483648LL) sr = -2147483648LL;

        // 다시 24bit MSB aligned 형태로 패킹
        uint32_t ul = (uint32_t)((int32_t)sl);
        uint32_t ur = (uint32_t)((int32_t)sr);
        tx_half_u16[4*i+0] = (uint16_t)(ul >> 16);
        tx_half_u16[4*i+1] = (uint16_t)(ul & 0xFFFF);
        tx_half_u16[4*i+2] = (uint16_t)(ur >> 16);
        tx_half_u16[4*i+3] = (uint16_t)(ur & 0xFFFF);

        ++s_met_pos;
    }

    if (s_met_pos >= MET_CLICK_LEN) {
        s_met_active = 0u;
    }
}


// 24-bit MSB aligned 2x16 → 32비트로 캐스팅
static inline int32_t u16x2_to_i32_24(uint16_t hi, uint16_t lo){
  uint32_t w = ((uint32_t)hi<<16) | lo;
  return (int32_t)w; // 상위 24비트 부호 유지
}
static inline void i32_24_to_u16x2(int32_t s, uint16_t *hi, uint16_t *lo){
  *hi = (uint16_t)(((uint32_t)s)>>16);
  *lo = (uint16_t)(((uint32_t)s)&0xFFFF);
}






// ---- 24-bit PCM helpers (unique names) ---------------------------------
static inline int32_t i24_from_u16x2(uint16_t hi, uint16_t lo){
  uint32_t w = ((uint32_t)hi<<16) | lo;
  return (int32_t)w; // 상위 24비트 부호 유지
}

static inline void i24_to_u16x2(int32_t s, uint16_t *hi, uint16_t *lo){
  *hi = (uint16_t)(((uint32_t)s) >> 16);
  *lo = (uint16_t)(((uint32_t)s) & 0xFFFF);
}

// Q31 → float
static inline float notch_q31_to_f32_uni(int32_t s){
  return ((float)s) * (1.0f/2147483648.0f); // 1/2^31
}

// float(-1..1) → Q31 (saturate)  ※ 이름 충돌 방지: notch_float_to_q31_sat
static inline int32_t notch_float_to_q31_sat(float x){
  if (x >  0.999999f) x =  0.999999f;
  if (x < -1.000000f) x = -1.000000f;
  return (int32_t)(x * 2147483647.0f);
}

// ---- I2S2 TX half-buffer overlay with I2S3 mic (with light AGC) --------
//  - tx_half_u16 : I2S2 TX 반버퍼 시작
//  - n_u16       : u16 원소 수(= BLOCK_SIZE_U16)
//  - which_half  : 0=전반부, 1=후반부 (RX half는 g_dma_half_mic3에 반영됨)
// ---- I2S2 TX half-buffer overlay with I2S3 mic (with light AGC) --------
//  - tx_half_u16 : I2S2 TX 반버퍼 시작
//  - n_u16       : u16 원소 수(= BLOCK_SIZE_U16)
//  - which_half  : 0=전반부, 1=후반부 (RX half는 g_dma_half_mic3에 반영됨)
static void mic3_overlay_mix_to_tx(uint16_t *tx_half_u16, uint32_t n_u16, uint8_t which_half)
{
  // RX 콜백이 마지막으로 쓴 반버퍼 기준으로 읽음 (스냅샷)
  uint8_t  mic_half  = g_dma_half_mic3;
  uint32_t roff_mic  = mic_half ? BLOCK_SIZE_U16 : 0;    // mic3RxBuf 오프셋

  if (!mic3_running) return;            // I2S3 OFF
  mic_balance_update_if_needed();       // g_mic_gain_lin 갱신
  if (g_mic_gain_lin <= 0.0f) return;   // 볼륨 0 → skip

  const uint16_t *mr   = &mic3RxBuf[roff_mic];
  const uint32_t  frames = n_u16 / 4u;  // 24bit L/R = u16 4개

  // --- (A) 아주 가벼운 블록 피크 기반 AGC (마이크만)  [원본 유지]
  float agc_mul = 1.0f;
  if (MicAGC_On) {
    int32_t maxabs = 0;
    for (uint32_t i=0; i<frames; ++i){
      int32_t ml   = (int32_t)(((uint32_t)mr[4*i+0]<<16) | mr[4*i+1]);
      int32_t mr24 = (int32_t)(((uint32_t)mr[4*i+2]<<16) | mr[4*i+3]);
      int32_t al = (ml<0)?-ml:ml, ar = (mr24<0)?-mr24:mr24;
      if (al > maxabs) maxabs = al;
      if (ar > maxabs) maxabs = ar;
    }
    float p = (float)maxabs * (1.0f/2147483648.0f); // ≈ peak
    const float target = 0.35f;                     // ≈ -9 dBFS
    if (p > 1e-7f){
      float g = target / p;
      if (g > 4.0f)  g = 4.0f;
      if (g < 0.25f) g = 0.25f;
      agc_mul = g;
    }
  }

  // --- (B) 총 마이크 게인: 감마 × dB 프리게인 × (AGC or 1)
  const float pre     = db2lin((float)MicBoost_dB);
  const float g_total = g_mic_gain_lin * pre * agc_mul;

  // --- (C) MicInputMode 분기: 0=스테레오, 1=L 확장, 2=R 확장
  uint32_t max_sum_abs = 0; // (옵션) VU 갱신용

  switch (MicInputMode & 3u) {

    case 1u: // L 확장: L만 받아 양 채널로
      for (uint32_t i=0; i<frames; ++i){
        // 마이크 24bit(Q31화) 추출
        int32_t mL_q = (int32_t)(((uint32_t)mr[4*i+0]<<16) | mr[4*i+1]);
        int32_t mR_q = (int32_t)(((uint32_t)mr[4*i+2]<<16) | mr[4*i+3]);

        float micL = notch_q31_to_f32(mL_q);
        float micR = notch_q31_to_f32(mR_q);

        // 한쪽만 0으로 들어오는 하드웨어 케이스 보정(기존 로직 유지)
        if (mL_q!=0 && mR_q==0) micR = micL; else if (mR_q!=0 && mL_q==0) micL = micR;

        // L만 모노 확장
        float xL = micL * g_total;
        float xR = micL * g_total;

        // 현재 TX L/R 24bit 읽기
        uint16_t Lhi = tx_half_u16[4*i+0], Llo = tx_half_u16[4*i+1];
        uint16_t Rhi = tx_half_u16[4*i+2], Rlo = tx_half_u16[4*i+3];
        int32_t txL_q = i24_from_u16x2(Lhi, Llo);
        int32_t txR_q = i24_from_u16x2(Rhi, Rlo);

        // float 누산 → Q31 포화
        float   yL = notch_q31_to_f32_uni(txL_q) + xL;
        float   yR = notch_q31_to_f32_uni(txR_q) + xR;
        int32_t outL_q = notch_float_to_q31_sat(yL);
        int32_t outR_q = notch_float_to_q31_sat(yR);

        // 다시 24bit로 패킹
        i24_to_u16x2(outL_q, &tx_half_u16[4*i+0], &tx_half_u16[4*i+1]);
        i24_to_u16x2(outR_q, &tx_half_u16[4*i+2], &tx_half_u16[4*i+3]);

        // (옵션) VU용
        uint32_t sum = (uint32_t)((mL_q<0)?-mL_q:mL_q) + (uint32_t)((mR_q<0)?-mR_q:mR_q);
        if (sum > max_sum_abs) max_sum_abs = sum;
      }
      break;

    case 2u: // R 확장: R만 받아 양 채널로
      for (uint32_t i=0; i<frames; ++i){
        int32_t mL_q = (int32_t)(((uint32_t)mr[4*i+0]<<16) | mr[4*i+1]);
        int32_t mR_q = (int32_t)(((uint32_t)mr[4*i+2]<<16) | mr[4*i+3]);

        float micL = notch_q31_to_f32(mL_q);
        float micR = notch_q31_to_f32(mR_q);

        if (mL_q!=0 && mR_q==0) micR = micL; else if (mR_q!=0 && mL_q==0) micL = micR;

        float xL = micR * g_total;
        float xR = micR * g_total;

        uint16_t Lhi = tx_half_u16[4*i+0], Llo = tx_half_u16[4*i+1];
        uint16_t Rhi = tx_half_u16[4*i+2], Rlo = tx_half_u16[4*i+3];
        int32_t txL_q = i24_from_u16x2(Lhi, Llo);
        int32_t txR_q = i24_from_u16x2(Rhi, Rlo);

        float   yL = notch_q31_to_f32_uni(txL_q) + xL;
        float   yR = notch_q31_to_f32_uni(txR_q) + xR;
        int32_t outL_q = notch_float_to_q31_sat(yL);
        int32_t outR_q = notch_float_to_q31_sat(yR);

        i24_to_u16x2(outL_q, &tx_half_u16[4*i+0], &tx_half_u16[4*i+1]);
        i24_to_u16x2(outR_q, &tx_half_u16[4*i+2], &tx_half_u16[4*i+3]);

        uint32_t sum = (uint32_t)((mL_q<0)?-mL_q:mL_q) + (uint32_t)((mR_q<0)?-mR_q:mR_q);
        if (sum > max_sum_abs) max_sum_abs = sum;
      }
      break;

    default: // 0 또는 기타: 스테레오 그대로
      for (uint32_t i=0; i<frames; ++i){
        int32_t mL_q = (int32_t)(((uint32_t)mr[4*i+0]<<16) | mr[4*i+1]);
        int32_t mR_q = (int32_t)(((uint32_t)mr[4*i+2]<<16) | mr[4*i+3]);

        float micL = notch_q31_to_f32(mL_q);
        float micR = notch_q31_to_f32(mR_q);

        if (mL_q!=0 && mR_q==0) micR = micL; else if (mR_q!=0 && mL_q==0) micL = micR;

        float xL = micL * g_total;
        float xR = micR * g_total;

        uint16_t Lhi = tx_half_u16[4*i+0], Llo = tx_half_u16[4*i+1];
        uint16_t Rhi = tx_half_u16[4*i+2], Rlo = tx_half_u16[4*i+3];
        int32_t txL_q = i24_from_u16x2(Lhi, Llo);
        int32_t txR_q = i24_from_u16x2(Rhi, Rlo);

        float   yL = notch_q31_to_f32_uni(txL_q) + xL;
        float   yR = notch_q31_to_f32_uni(txR_q) + xR;
        int32_t outL_q = notch_float_to_q31_sat(yL);
        int32_t outR_q = notch_float_to_q31_sat(yR);

        i24_to_u16x2(outL_q, &tx_half_u16[4*i+0], &tx_half_u16[4*i+1]);
        i24_to_u16x2(outR_q, &tx_half_u16[4*i+2], &tx_half_u16[4*i+3]);

        uint32_t sum = (uint32_t)((mL_q<0)?-mL_q:mL_q) + (uint32_t)((mR_q<0)?-mR_q:mR_q);
        if (sum > max_sum_abs) max_sum_abs = sum;
      }
      break;
  }

  // --- (D) (옵션) 미니 VU(0.8) 갱신: float 경로와 동일 로직
  {
    float p = (float)max_sum_abs * (1.0f / (2.0f * 2147483648.0f));
    if (p < 1e-12f) p = 1e-12f;
    float dB = 20.0f * log10f(p);
    if (dB < MIC_VU_GATE_DB) {
      s_vu_mic8 = 0;
    } else {
      if (dB < MIC_VU_FLOOR_DB) dB = MIC_VU_FLOOR_DB;
      if (dB > 0.0f)            dB = 0.0f;
      uint32_t vu = (uint32_t)((dB - MIC_VU_FLOOR_DB) / (-MIC_VU_FLOOR_DB) * 255.0f + 0.5f);
      if (vu > 255u) vu = 255u;
      s_vu_mic8 = (uint8_t)vu;
    }
  }
}














// ★ float 도메인에서 s_bufL/R에 마이크(I2S3)를 섞는다(최종 패킹 이전).
//   frames: 스테레오 프레임 수(=512), which_half: 0/1 (DMA half)
 void mic3_mix_into_float(float *sL, float *sR, uint32_t frames, uint8_t which_half)
{
    if (!mic3_running) return;

    mic_balance_update_if_needed();           // SoundBalance 감마 → g_mic_gain_lin
    if (g_mic_gain_lin <= 0.0f) return;      // 마이크 볼륨 0 → skip

    uint8_t  mic_half = g_dma_half_mic3;                   // RX 콜백에서 갱신됨
    uint32_t roff     = mic_half ? BLOCK_SIZE_U16 : 0u;
    const uint16_t *mr = &mic3RxBuf[roff];


    // --- (A) 블록 피크 기반 아주 가벼운 AGC (선택/현행 유지) ---
    // --- (A’) RMS 기반 AGC (채널-링크, 노이즈게이트, 어택/릴리즈 분리) ---
    static float s_agc_lin = 1.0f;     // AGC 배율(선형)
    static float s_rms_ema = 0.0f;     // 블록 간 RMS 추적(EMA)

    float agc_mul = 1.0f;
    if (MicAGC_On) {
        // 1) 프리패스: 디메이션하여 RMS 추정 (채널 링크)
        const uint32_t HOP = 8;
        double acc2 = 0.0; uint32_t cnt = 0;

        for (uint32_t i=0; i<frames; i += HOP){
            // 24bit 추출
            int32_t mLq = (int32_t)(((uint32_t)mr[4*i+0]<<16)|mr[4*i+1]);
            int32_t mRq = (int32_t)(((uint32_t)mr[4*i+2]<<16)|mr[4*i+3]);

            // MicInputMode에 맞춰 AGC 검출용 샘플 선택(채널-링크)
            // 0=stereo(평균), 1=L모노, 2=R모노
            float x;
            if ((MicInputMode & 3u)==1u) { x = notch_q31_to_f32(mLq); }
            else if ((MicInputMode & 3u)==2u) { x = notch_q31_to_f32(mRq); }
            else { x = 0.5f*(notch_q31_to_f32(mLq)+notch_q31_to_f32(mRq)); }

            acc2 += (double)x * (double)x;
            ++cnt;
        }

        float rms_blk = (cnt>0) ? sqrtf((float)(acc2/(double)cnt)) : 0.0f;

        // 2) 노이즈 게이트: 너무 작은 신호는 킥업 금지(노이즈만 올리는 거 방지)
        //    게이트는 약 -60 dBFS, 해제 히스테리시스 -54 dBFS
        const float GATE_ON_RMS  = powf(10.f, -60.f/20.f); // ≈0.001
        const float GATE_REL_RMS = powf(10.f, -54.f/20.f);

        // RMS EMA로 조금 더 안정화
        const float emaA = 0.25f;
        s_rms_ema = (1.0f-emaA)*s_rms_ema + emaA*rms_blk;
        float rms_use = s_rms_ema;

        // 3) 타겟 RMS와 소프트니 (약 -12 dBFS 기준)
        const float target = powf(10.f, -12.f/20.f);      // ≈ 0.25
        const float knee   = powf(10.f,  -3.f/20.f) * target; // ±3 dB 부근에서 완만하게

        // 게이트 처리
        uint8_t gate_open = (rms_use >= GATE_REL_RMS);
        if (!gate_open && rms_use < GATE_ON_RMS) {
            // 게이트 닫힘: 증폭 금지(=1.0로 붙들기)
            agc_mul = 1.0f;
        } else {
            // 4) 원하는 선형 이득: target / rms
            float desired = (rms_use > 1e-7f) ? (target / rms_use) : 1.0f;

            // 소프트니: target±3dB 구간에서 완만(니 함수 흉내, 간단 버전)
            if (rms_use > target/knee && rms_use < target*knee) {
                float t = (rms_use - target/knee) / (target*knee - target/knee); // 0..1
                // 경사 완화: desired를 1과 선형보정 사이에서 보간
                desired = 1.0f + t*(desired - 1.0f);
            }

            // 하드클램프: -18..+12 dB 범위
            const float MIN_G = powf(10.f, -18.f/20.f);  // ≈ 0.125
            const float MAX_G = powf(10.f, +12.f/20.f);  // ≈ 4.0
            if (desired < MIN_G) desired = MIN_G;
            if (desired > MAX_G) desired = MAX_G;

            // 5) 어택/릴리즈 분리(크게 줄일 땐 빠르게, 늘릴 땐 천천히)
            float alpha_att = 0.35f;  // 빠른 어택
            float alpha_rel = 0.08f;  // 느린 릴리즈
            float alpha = (desired < s_agc_lin) ? alpha_att : alpha_rel;

            s_agc_lin = (1.0f - alpha)*s_agc_lin + alpha*desired;
            agc_mul   = s_agc_lin;
        }
    }

    // --- (B’) 총 마이크 게인: 감마(SoundBalance) × MicBoost_dB × AGC × 헤드룸 ---
    const float pre = db2lin((float)MicBoost_dB);          // UI 프리게인(그대로 유지)
    const float headroom = powf(10.f, -0.8f/20.f);         // -0.8 dB 헤드룸(버스 보호)
    const float g_total = g_mic_gain_lin * pre * agc_mul * headroom;

    // --- (C) 모노/스테레오 분기하여 float 버퍼에 가산 ---
    uint32_t max_sum_abs = 0; // VU(0..8) 용

    // MicInputMode: 0=스테레오, 1=L 확장(모노), 2=R 확장(모노)
    switch (MicInputMode & 3u) {
      case 1u: // L 확장: L만 받아 양 채널로
        for (uint32_t i=0; i<frames; ++i){
            int32_t mL = mic3_unpack_u16x2(mr[4*i+0], mr[4*i+1]);
            int32_t mR = mic3_unpack_u16x2(mr[4*i+2], mr[4*i+3]);
            float micL = notch_q31_to_f32(mL), micR = notch_q31_to_f32(mR);
            /*
             *
            int32_t mL = (int32_t)(((uint32_t)mr[4*i+0]<<16) | mr[4*i+1]);
            int32_t mR = (int32_t)(((uint32_t)mr[4*i+2]<<16) | mr[4*i+3]);
            해당 줄 안되면 이걸로 고치세요.
             */


            // 한쪽만 0으로 들어오는 하드웨어 케이스 보정(기존 로직 유지)
            if (mL!=0 && mR==0) micR = micL; else if (mR!=0 && mL==0) micL = micR;

            float mono = micL;  // L만 사용
            sL[i] += mono * g_total;
            sR[i] += mono * g_total;

            uint32_t sum = (uint32_t)((mL<0)?-mL:mL) + (uint32_t)((mR<0)?-mR:mR);
            if (sum > max_sum_abs) max_sum_abs = sum;
        }
        break;

      case 2u: // R 확장: R만 받아 양 채널로
        for (uint32_t i=0; i<frames; ++i){
            int32_t mL = mic3_unpack_u16x2(mr[4*i+0], mr[4*i+1]);
            int32_t mR = mic3_unpack_u16x2(mr[4*i+2], mr[4*i+3]);
            float micL = notch_q31_to_f32(mL), micR = notch_q31_to_f32(mR);
            if (mL!=0 && mR==0) micR = micL; else if (mR!=0 && mL==0) micL = micR;

            float mono = micR;  // R만 사용
            sL[i] += mono * g_total;
            sR[i] += mono * g_total;

            uint32_t sum = (uint32_t)((mL<0)?-mL:mL) + (uint32_t)((mR<0)?-mR:mR);
            if (sum > max_sum_abs) max_sum_abs = sum;
        }
        break;

      default: // 0 또는 기타: 스테레오 그대로
        for (uint32_t i=0; i<frames; ++i){
            int32_t mL = mic3_unpack_u16x2(mr[4*i+0], mr[4*i+1]);
            int32_t mR = mic3_unpack_u16x2(mr[4*i+2], mr[4*i+3]);
            float micL = notch_q31_to_f32(mL), micR = notch_q31_to_f32(mR);
            if (mL!=0 && mR==0) micR = micL; else if (mR!=0 && mL==0) micL = micR;

            sL[i] += micL * g_total;
            sR[i] += micR * g_total;

            uint32_t sum = (uint32_t)((mL<0)?-mL:mL) + (uint32_t)((mR<0)?-mR:mR);
            if (sum > max_sum_abs) max_sum_abs = sum;
        }
        break;
    }


    // --- (D) 미니 VU(0..8) 갱신(현행 유지) ---
    {
        float p = (float)max_sum_abs * (1.0f / (2.0f * 2147483648.0f));
        if (p < 1e-12f) p = 1e-12f;
        float dB = 20.0f * log10f(p);
        if (dB < MIC_VU_GATE_DB) {
            s_vu_mic8 = 0;
        } else {
            if (dB < MIC_VU_FLOOR_DB) dB = MIC_VU_FLOOR_DB;
            if (dB > 0.0f)            dB = 0.0f;
            float frac = (dB - MIC_VU_FLOOR_DB) / (-MIC_VU_FLOOR_DB);
            uint8_t seg = (uint8_t)(frac * 8.0f + 0.5f);
            if (seg > 8) seg = 8;
            s_vu_mic8 = seg;
        }
    }
}







///=======================================================================================
// [4] DSP MODULES
//   4-1) NOTCH (Bank A/B, DF2T, -a1,-a2)
///=======================================================================================


// 🔁 파라미터 변동 시에만 BankA/B 재설계 (반버퍼 경계에서 호출)
void notch_iir_update_if_needed(void)
{
  uint8_t  on = CutOffOnOff ? 1u : 0u;
  uint32_t fS = CutOffFreqStart;
  uint32_t fE = CutOffFreqEnd;

  if (on != g_prev_on || fS != g_prev_fS || fE != g_prev_fE) {
    g_prev_on = on; g_prev_fS = fS; g_prev_fE = fE;

    if (on) {
      dsp_style_notch_design_from_ui(); // BankA/B 계수&상태 init
    } else {
      // OFF → 상태만 클리어 (패스스루)
      memset(g_notchA_stateL, 0, sizeof(g_notchA_stateL));
      memset(g_notchA_stateR, 0, sizeof(g_notchA_stateR));
      memset(g_notchB_stateL, 0, sizeof(g_notchB_stateL));
      memset(g_notchB_stateR, 0, sizeof(g_notchB_stateR));
    }
  }
}

// 🎚️ IIR 노치(+볼륨) 블록 처리 (BankA → BankB)
void notch_process_with_iir_and_volume(const uint16_t *src16, uint16_t *dst16, uint32_t n_u16)
{
  notch_iir_update_if_needed();       // 반버퍼 경계에서 파라미터 반영
  notch_volume_update_if_needed();

  const uint32_t frames = n_u16 / 4;  // half-buffer = 512 frames

  // 1) 24->float 언팩
  for (uint32_t i = 0; i < frames; ++i) {
    uint32_t wL = ((uint32_t)src16[4*i+0] << 16) | src16[4*i+1];
    uint32_t wR = ((uint32_t)src16[4*i+2] << 16) | src16[4*i+3];
    int32_t  sL = (int32_t)wL;
    int32_t  sR = (int32_t)wR;
    s_bufL[i] = (float)sL * (1.0f/2147483648.0f);
    s_bufR[i] = (float)sR * (1.0f/2147483648.0f);
  }

//////////////////// 여기서부터 모드에 따른 분기 시작 /////////////////////////////////
  if (IsSoundGenReady == 1) {
      sg_process(s_bufL, s_bufR, frames);
  } else{


	  // 2) NOTCH on → BankA → (있으면) BankB
	  if (CutOffOnOff == 1) {
		if (g_notchA_stages > 0) {
		  arm_biquad_cascade_df2T_f32(&g_notchA_L, s_bufL, s_bufL, frames);
		  arm_biquad_cascade_df2T_f32(&g_notchA_R, s_bufR, s_bufR, frames);
		}
		if (g_notchB_stages > 0) {
		  arm_biquad_cascade_df2T_f32(&g_notchB_L, s_bufL, s_bufL, frames);
		  arm_biquad_cascade_df2T_f32(&g_notchB_R, s_bufR, s_bufR, frames);
		}
	  } else if (CutOffOnOff == 2) {
			// 입력(s_bufL/R) → 출력(s_bufL/R) in-place 처리
			ps_process(s_bufL, s_bufR, s_bufL, s_bufR, frames);
			// 이후는 공통: 출력 볼륨 → 24비트 리팩
	  } else{}

  }


//////////////////// 여기서부터 분기 끝, 볼륨 시작 /////////////////////////////////



  // 3) 출력 볼륨(감마 이득)
  const float g = g_vol_lin;
  if (g != 1.0f) {
    if (g == 0.0f) {
      memset(s_bufL, 0, sizeof(float)*frames);
      memset(s_bufR, 0, sizeof(float)*frames);
    } else {
      for (uint32_t i = 0; i < frames; ++i) { s_bufL[i] *= g; s_bufR[i] *= g; }
    }
  }

  // --- [NEW] I2S3 마이크를 float 버퍼에 직접 섞기(양자화 1회로 축소) ---
  mic3_mix_into_float(s_bufL, s_bufR, frames, g_dma_half);
  met_click_mix_block(s_bufL, s_bufR, frames);   // [ADD] 메트로놈 클릭 합산(패킹 직전)




  // 4) float -> 24bit MSB aligned
  for (uint32_t i = 0; i < frames; ++i) {
    float xL = s_bufL[i], xR = s_bufR[i];
    if (xL >  0.999999f) xL =  0.999999f; else if (xL < -0.999999f) xL = -0.999999f;
    if (xR >  0.999999f) xR =  0.999999f; else if (xR < -0.999999f) xR = -0.999999f;
    int32_t yL = (int32_t)(xL * 2147483647.0f);
    int32_t yR = (int32_t)(xR * 2147483647.0f);
    dst16[4*i+0] = (uint16_t)((uint32_t)yL >> 16);
    dst16[4*i+1] = (uint16_t)((uint32_t)yL & 0xFFFF);
    dst16[4*i+2] = (uint16_t)((uint32_t)yR >> 16);
    dst16[4*i+3] = (uint16_t)((uint32_t)yR & 0xFFFF);
  }
}




//===================================================================================================================
//============================== PITCH SHIFTER MODULE ( TIME-DOMAIN / DUAL-READ XFADE ) =============================
//===================================================================================================================

/* ★ UI: CutOffOnOff == 2 일 때 활성화 (0=패스스루, 1=노치, 2=피치)                      */
/* ★ Semitone 인덱스는 UI 변수를 그대로 사용: PitchSemitone in [0..6], 3이 중립(0 semitone) */

#ifndef PS_BUF_SIZE
  #define PS_BUF_SIZE   3072   // 링버퍼 크기(프레임): 48kHz에서 안전한 기본값
#endif
#ifndef PS_OVERLAP
  #define PS_OVERLAP    384    // 오버랩 길이(샘플): 120~200 사이 튜닝 가능
#endif

// 내부 상태
float ps_buf[PS_BUF_SIZE];
int   ps_wptr = 0;
float ps_rptr = 0.0f;     // fractional
float ps_shift_z = 1.0f;  // ratio smoothing

// HPF(전처리) 계수/상태 (DF2T: {b0,b1,b2,-a1,-a2})
float ps_hpf_c[5];
float ps_hpf_z1 = 0.0f, ps_hpf_z2 = 0.0f;

// === 보조 유틸 ===
static inline int wrap_i(int i, int N){ while(i<0) i+=N; while(i>=N) i-=N; return i; }

static inline float xfade_eqpow(float x01) // equal-power crossfade 0..1
{
  if (x01 <= 0.f) return 0.f;
  if (x01 >= 1.f) return 1.f;
  return 0.5f - 0.5f*cosf((float)PI * x01);
}

static inline float sat01(float x){ return (x<=0.f)?0.f:((x>=1.f)?1.f:x); }


// 4-tap Cubic 보간 (Catmull-Rom), 원형버퍼 상 pos=정수+분수
static inline float circ_cubic(const float *buf, int N, float pos)
{
  int i0 = (int)floorf(pos);
  float t = pos - (float)i0;
  int im1 = wrap_i(i0-1, N);
  int i1  = wrap_i(i0+1, N);
  int i2  = wrap_i(i0+2, N);

  float ym1 = buf[im1];
  float y0  = buf[wrap_i(i0, N)];
  float y1  = buf[i1];
  float y2  = buf[i2];

  float a0 = -0.5f*ym1 + 1.5f*y0 - 1.5f*y1 + 0.5f*y2;
  float a1 =  1.0f*ym1 - 2.5f*y0 + 2.0f*y1 - 0.5f*y2;
  float a2 = -0.5f*ym1 + 0.5f*y1;
  float a3 =  y0;
  return ((a0*t + a1)*t + a2)*t + a3;
}

// 1-tap Linear 보간 (안정, 오버슈트 無)
static inline float circ_linear(const float *buf, int N, float pos)
{
  int i0 = (int)floorf(pos);
  float t = pos - (float)i0;
  float y0 = buf[wrap_i(i0,   N)];
  float y1 = buf[wrap_i(i0+1, N)];
  return y0 + t*(y1 - y0);
}


// 메인(DSP부)에서 쓰던 HPF 설계 유틸 복제: RBJ 2차 HPF, DF2T 계수 {b0,b1,b2,-a1,-a2}
void ps_make_hpf2_df2t(float fc, float Q, float fs, float *c /*5개*/)
{
  float w0 = 2.f*(float)PI * (fc/fs);
  float cw = arm_cos_f32(w0);
  float sw = arm_sin_f32(w0);
  float alpha = sw/(2.f*Q);

  float b0 = (1.f + cw)*0.5f;
  float b1 = -(1.f + cw);
  float b2 = (1.f + cw)*0.5f;
  float a0 =  1.f + alpha;
  float a1 = -2.f*cw;
  float a2 =  1.f - alpha;

  float b0n = b0/a0, b1n = b1/a0, b2n = b2/a0;
  float a1n = a1/a0, a2n = a2/a0;

  c[0]=b0n; c[1]=b1n; c[2]=b2n; c[3]=-a1n; c[4]=-a2n; // DF2T 포맷( -a1, -a2 )
}

// 세미톤 인덱스(0..6, 3이 중립) → 배속 ratio = 2^(semitone/12)
static inline float ps_ratio_from_ui(int8_t pitchIndex /*0..6*/)
{
  int semitone = ((int)pitchIndex) - 3;   // 3→0st, 0→-3st, 6→+3st
  if (semitone < -3) semitone = -3;
  if (semitone >  3) semitone =  3;
  return powf(2.0f, ((float)semitone)/12.0f);
}

// DF2T 1-샘플 바이쿼드 (coeffs = {b0,b1,b2,-a1,-a2})
static inline float biquad_df2t_1s(const float *c, float x, float *z1, float *z2)
{
  float y      = c[0]*x + *z1;
  float new_z1 = c[1]*x + *z2 + c[3]*y;  // c[3] = -a1
  float new_z2 = c[2]*x        + c[4]*y; // c[4] = -a2
  *z1 = new_z1; *z2 = new_z2;
  return y;
}

// 초기화 (샘플레이트/부팅 시 1회, 또는 모드 진입 시)
void ps_init(uint32_t fs_hz)
{
  ps_make_hpf2_df2t(300.0f, 0.70710678f, (float)fs_hz, ps_hpf_c);
  ps_hpf_z1 = ps_hpf_z2 = 0.0f;
  memset(ps_buf, 0, sizeof(ps_buf));
  ps_wptr = 0; ps_rptr = 0.0f; ps_shift_z = 1.0f;
}

// 블록 처리: 입력 L/R(float, -1..1) → 모노 처리 → 스테레오 동일 출력
void ps_process(const float *Lin, const float *Rin,
                float *Lout, float *Rout, uint32_t n)
{
  float target = ps_ratio_from_ui(PitchSemitone);     // UI 연동
  // 부드러운 스무딩(지퍼노이즈 방지)
  ps_shift_z  += 0.004f  * (target - ps_shift_z);
  float shift  = ps_shift_z;
  if (shift < 0.50f) shift = 0.50f;
  if (shift > 2.00f) shift = 2.00f;

  // 원피치면 그냥 바이패스 (톤 컬러 변형 없음)
  if (fabsf(shift - 1.0f) < 1e-3f) {
    memcpy(Lout, Lin, sizeof(float)*n);
    memcpy(Rout, Rin, sizeof(float)*n);
    return;
  }

  // 블록 로컬 DC 블로커 상태
  float dc_x1 = 0.f, dc_y1 = 0.f;

  for (uint32_t i=0; i<n; ++i)
  {
    // ① 모노합
    //float m = Lin[i] + Rin[i];
	  float m = 0.5f * (Lin[i] + Rin[i]);   // 내부 헤드룸 확보

    // ② 전처리 HPF(수치 안정/잡음 억제), 보수적으로 DC 블로커 겸용
    //    (필요시 아래 DC블로커 라인으로 대체 가능)
    m = biquad_df2t_1s(ps_hpf_c, m, &ps_hpf_z1, &ps_hpf_z2);
    // float ydc = (m - dc_x1) + 0.995f*dc_y1; dc_x1 = m; dc_y1 = ydc; m = ydc;

    // ③ 링버퍼 기록
    ps_buf[ps_wptr] = m;

    // ④ 읽기 포지션 2개(0/180°)에서 Cubic 보간
    float pos0 = ps_rptr;
    float pos1 = ps_rptr + 0.5f*(float)PS_BUF_SIZE;
    if (pos1 >= (float)PS_BUF_SIZE) pos1 -= (float)PS_BUF_SIZE;

    float s0 = circ_linear(ps_buf, PS_BUF_SIZE, pos0);
    float s1 = circ_linear(ps_buf, PS_BUF_SIZE, pos1);


    // ⑤ 오버랩 진행도 기반 equal-power 크로스페이드
    int rd0 = (int)floorf(pos0);
    int rd1 = (int)floorf(pos1);
    int d0  = ps_wptr - rd0; if (d0 < 0) d0 += PS_BUF_SIZE;
    int d1  = ps_wptr - rd1; if (d1 < 0) d1 += PS_BUF_SIZE;

    // 연속 윈도우: GUARD 이후부터 부드럽게 올라오는 equal-power 페이드
    const int GUARD = 12; // 8~16 권장: 너무 가까운 구간은 강제 0
    float t0 = sat01(( (float)d0 - (float)GUARD ) / (float)PS_OVERLAP);
    float t1 = sat01(( (float)d1 - (float)GUARD ) / (float)PS_OVERLAP);
    float w0 = xfade_eqpow(t0);
    float w1 = xfade_eqpow(t1);

    // 양쪽 다 0에 너무 가깝다면(초근접) 더 먼 쪽을 선택
    if (w0 < 1e-6f && w1 < 1e-6f) {
        if (d0 <= d1) w1 = 1.f; else w0 = 1.f;
    }

    float ws = w0 + w1; if (ws < 1e-6f) ws = 1.f;
    float y  = (s0*w0 + s1*w1) / ws;


    // ⑥ 포인터 전진
    ps_rptr += shift;
    if (ps_rptr >= (float)PS_BUF_SIZE) ps_rptr -= (float)PS_BUF_SIZE;
    ps_wptr++; if (ps_wptr >= PS_BUF_SIZE) ps_wptr = 0;


    if (y >  1.2f) y =  1.2f;
    if (y < -1.2f) y = -1.2f;

    // ⑦ 스테레오 동일 출력
    Lout[i] = y; Rout[i] = y;
  }
}



//===================================================================================================================
//================================= SOUND GENERATOR (TUNER TONE) =================================
//  - UI부(main.c)에서 SoundFrequencyOutput(Hz)을 계산하면 그것을 '그대로' 사용
//  - 만약 0.0이면 CurrentNoteIndex/TunerBaseFreq로 보조 계산
//  - 출력부(볼륨 감마+24비트 패킹)는 기존 파이프라인 유지
//===================================================================================================================

static float sg_phaseL=0.0f, sg_phaseR=0.0f;
static float sg_phase_inc_z=0.0f;    // 스무딩된 phase 증가량
static float sg_env=0.0f;            // 클릭 방지용 간단 A/R

static inline float sg_note_hz(void){
  float f = SoundFrequencyOutput;                 // ★ UI가 계산한 Hz 우선
  if (f <= 0.0f) {
    // 보조 계산: A4(인덱스 48)를 기준으로 반음 단위
    int n = (int)CurrentNoteIndex - 48;          // UI부 NoteNames 테이블 기준
    f = (float)TunerBaseFreq * powf(2.0f, (float)n/12.0f);
  }
  if (f < 20.0f) f = 20.0f;
  if (f > (float)g_fs_hz*0.45f) f = (float)g_fs_hz*0.45f; // 나이퀴스트 근처 방어
  return f;
}

void sg_init(uint32_t fs_hz){
  (void)fs_hz;
  sg_phaseL = sg_phaseR = 0.0f;
  sg_phase_inc_z = 0.0f;
  sg_env = 0.0f;
}

// equal-power crossfade 보정(필요 시 사용). 여기선 간단 A/R만 씀.
// static inline float xfade_eqpow(float x01){ return 0.5f - 0.5f*cosf((float)PI * fminf(fmaxf(x01,0.f),1.f)); }

// 1블록 생성: 입력은 무시하고 s_bufL/R에 직접 합성
void sg_process(float *L, float *R, uint32_t n)
{
  // 목표 위상 증가량(= f/fs) 스무딩: 지퍼노이즈 억제
  float target_inc = sg_note_hz() / (float)g_fs_hz;
  if (target_inc < 1e-6f) target_inc = 1e-6f;
  sg_phase_inc_z += 0.02f * (target_inc - sg_phase_inc_z);

  // 간단 attack/release
  float target_env = 1.0f;
  const float a = 0.75f;  // 마스터 볼륨 이전의 합성 진폭

  for (uint32_t i=0; i<n; ++i){
    sg_env += 0.01f * (target_env - sg_env);

    float y;
    switch (SoundGenMode){
      case 1: { // square
        float s = arm_sin_f32(2.0f*(float)PI*sg_phaseL);
        y = (s >= 0.f ? 1.f : -1.f);
        break;
      }
      case 2: { // triangle (0..1 삼각파 → -1..1 스케일)
        float p = sg_phaseL - floorf(sg_phaseL);
        y = 4.0f*fabsf(p - 0.5f) - 1.0f;
        break;
      }
      default: // sine
        y = arm_sin_f32(2.0f*(float)PI*sg_phaseL);
        break;
    }

    y *= (a * sg_env);
    L[i] = y;
    R[i] = y;

    sg_phaseL += sg_phase_inc_z;
    sg_phaseR += sg_phase_inc_z;
    if (sg_phaseL >= 1.0f) sg_phaseL -= 1.0f;
    if (sg_phaseR >= 1.0f) sg_phaseR -= 1.0f;
  }
}





















///=======================================================================================
//================= VU METER =======================
///=======================================================================================
static volatile uint8_t s_vu_enabled = 0;
static volatile uint8_t s_vuL_segs = 0; // 0..30 (2세그 = 1칸)
static volatile uint8_t s_vuR_segs = 0; // 0..30


// UI부에서 켜고 끄기
void notch_set_vu_enabled(uint8_t on) { s_vu_enabled = on ? 1 : 0; if(!s_vu_enabled){ s_vuL_segs=0; s_vuR_segs=0; } }
// UI부가 읽어가기
void notch_get_vu_segments(uint8_t *L, uint8_t *R) { if(L)*L=s_vuL_segs; if(R)*R=s_vuR_segs; }


// half-버퍼 기준으로 L/R 피크 근사 → 0..30 세그 계산
static inline void vu_compute_on_block(uint16_t *rx, uint32_t halfwords)
{
    if (!s_vu_enabled) return;

    // 24bit 스테레오(프레임당 halfword 4개) 가정:
    // [L_hi16][L_lo8x][R_hi16][R_lo8x] 형태일 때 hi16만으로 근사 피크 사용
    int32_t maxL = 0, maxR = 0;

    // halfwords는 half-buffer 길이(u16 단위)
    for (uint32_t i = 0; i + 3 < halfwords; i += 4) {
        int16_t l = (int16_t)rx[i + 0]; // L hi16 근사
        int16_t r = (int16_t)rx[i + 2]; // R hi16 근사
        int32_t al = (l < 0) ? -l : l;
        int32_t ar = (r < 0) ? -r : r;
        if (al > maxL) maxL = al;
        if (ar > maxR) maxR = ar;
    }

    // 16bit 최대치 기준 정규화 → dBFS → 0..30 세그
    // -60dB..0dB 범위를 0..30으로 선형 매핑
    const float FLOOR_DB = -60.0f;

    float nL = (float)maxL / 32768.0f;
    float nR = (float)maxR / 32768.0f;
    if (nL < 1e-6f) nL = 1e-6f;
    if (nR < 1e-6f) nR = 1e-6f;

    float dBL = 20.0f * log10f(nL);
    float dBR = 20.0f * log10f(nR);
    if (dBL < FLOOR_DB) dBL = FLOOR_DB;
    if (dBR < FLOOR_DB) dBR = FLOOR_DB;

    float fL = (dBL - FLOOR_DB) / (-FLOOR_DB); // 0..1
    float fR = (dBR - FLOOR_DB) / (-FLOOR_DB); // 0..1

    uint8_t segL = (uint8_t)(fL * 30.0f + 0.5f);
    uint8_t segR = (uint8_t)(fR * 30.0f + 0.5f);
    if (segL > 30) segL = 30;
    if (segR > 30) segR = 30;

    s_vuL_segs = segL;
    s_vuR_segs = segR;
}
///=======================================================================================
///=======================================================================================






///=======================================================================================
///================ DMA CALL BACK SECTION!@ ===========================
///=======================================================================================
// === DMA 이중버퍼 설정 (24bit 스테레오 = half-word 4개/프레임) ===


// notch.c 상단의 내부 정적 함수 근처에 추가
// === DMA 이중버퍼 렌더: 분기 전부 제거, float 경로만 사용 ===
// === DMA 이중버퍼 렌더: 경로 통일(항상 float 믹스), APR=0은 i2s2만 뮤트 ===
static inline void notch_render_half(uint32_t roff)
{
    const uint8_t half = (roff ? 1u : 0u);
    const uint32_t frames = BLOCK_SIZE_U16 / 4u;

    // 0) SoundGen 최우선: 켜지면 외부 입력 의미상 차단(APR=0) + 기존 파이프라인으로 생성
    if (IsSoundGenReady == 1) {
        // UI/상태 일관성을 위해 매 블록마다 APR을 0으로 내려둔다
        AudioProcessingIsReady = 0;

        // float 파이프라인 내부에서 sg_process→(필요시)DSP→MasterVolume→mic3→metronome→pack
        notch_process_with_iir_and_volume(&rxBuf[roff], &txBuf[roff], BLOCK_SIZE_U16);
        return;
    }

    if (!AudioProcessingIsReady) {
        // 0) 작업용 float 버퍼 초기화 (정적 버퍼만 사용: 스택 사용 0)
        const uint32_t frames = BLOCK_SIZE_U16 / 4u;
        for (uint32_t i = 0; i < frames; ++i) { s_tmpL[i] = 0.0f; s_tmpR[i] = 0.0f; }

        // 1) 마이크(i2s3) → float 도메인으로 합성 (AGC/모드/사운드밸런스 포함)
        //    half는 반버퍼 인덱스(0/1). 기존 notch.c의 마이크 믹서 그대로 호출.
        mic3_mix_into_float(s_tmpL, s_tmpR, frames, half);

        // 2) 메트로놈 클릭도 float로 합성 (MasterVolume은 내부에서 반영됨)
        met_click_mix_block(s_tmpL, s_tmpR, frames);

        // 3) float → Q31 포화 변환 후 I2S2 TX에 패킹
        uint16_t* __restrict tx = &txBuf[roff];
        for (uint32_t i = 0; i < frames; ++i) {
            int32_t qL = notch_float_to_q31_sat(s_tmpL[i]);
            int32_t qR = notch_float_to_q31_sat(s_tmpR[i]);
            uint32_t ul = (uint32_t)qL;
            uint32_t ur = (uint32_t)qR;
            tx[4*i + 0] = (uint16_t)(ul >> 16);
            tx[4*i + 1] = (uint16_t)(ul & 0xFFFF);
            tx[4*i + 2] = (uint16_t)(ur >> 16);
            tx[4*i + 3] = (uint16_t)(ur & 0xFFFF);
        }
        return;
    }

    // APR=1: 기존 float 파이프라인 그대로 (i2s2 처리 + 마스터볼륨 + mic3 + metronome)
    notch_process_with_iir_and_volume(&rxBuf[roff], &txBuf[roff], BLOCK_SIZE_U16);
}




// === DMA 콜백: HAL은 weak로 제공 → 여기서 오버라이드 가능 ===
// === DMA 콜백: HAL weak 오버라이드 ===
void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &hi2s2) {
        cb_state = 1;
        g_dma_half = 0;

        vu_compute_on_block(&rxBuf[0], BLOCK_SIZE_U16);
        notch_render_half(0u);     // 내부에서 모든 분기 처리

        // UI SFX만 24bit 오버레이(마스터볼륨 반영) - 기존 그대로 유지
        ui_sfx_overlay_mix_to_tx(&txBuf[0], BLOCK_SIZE_U16);
    }
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &hi2s2) {
        cb_state = 2;
        g_dma_half = 1;

        vu_compute_on_block(&rxBuf[BLOCK_SIZE_U16], BLOCK_SIZE_U16);
        notch_render_half(BLOCK_SIZE_U16);

        ui_sfx_overlay_mix_to_tx(&txBuf[BLOCK_SIZE_U16], BLOCK_SIZE_U16);
    }
}



// --- I2S3 RX 콜백(튜너 캡처 전용) ---

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &hi2s3) {
        g_dma_half_mic3 = 0;

        // 지금 쓰고 있는 버퍼
        uint8_t cur = s_i2s3_cur;

        // DMA가 실제로 채운 앞 절반만 안전 버퍼로 복사
        memcpy(&s_i2s3_safe[cur][0],
               &mic3RxBuf[0],
               I2S3_HALF_U16 * sizeof(uint16_t));

        // 여기서는 아직 VU/튜너 호출 안 한다.
        // 왜냐면 뒤 절반이 아직 안 붙었으니까.
    }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &hi2s3) {
        g_dma_half_mic3 = 1;

        uint8_t cur = s_i2s3_cur;

        // 뒤 절반을 같은 버퍼의 뒤쪽에 붙인다
        memcpy(&s_i2s3_safe[cur][I2S3_HALF_U16],
               &mic3RxBuf[I2S3_HALF_U16],
               I2S3_HALF_U16 * sizeof(uint16_t));

        // 이제 한 덩어리(BLOCK_SIZE_U16) 다 모였으니까
        // 이 시점에서만 VU/튜너에 넘긴다
        mic3_update_mini_vu_from_buf(s_i2s3_safe[cur], I2S3_DMA_U16);
        Tuner_FeedInterleavedI2S24(s_i2s3_safe[cur], I2S3_DMA_U16);

        // 다음 DMA 사이클은 다른 버퍼로
        s_i2s3_cur ^= 1;
    }
}


///=======================================================================================
///========================== AUDIO FINAL OUTPUT OPREATION ====================================
///=======================================================================================

static inline void dsp_enable_ftz_dn(void)
{
#if defined(__FPU_PRESENT) && (__FPU_PRESENT == 1) && \
    defined(__FPU_USED)    && (__FPU_USED    == 1)
    uint32_t fpscr = __get_FPSCR();
    fpscr |= (1u << 24); // FZ  : Flush-to-zero
    fpscr |= (1u << 19); // DN  : Default NaN (denormals treated as zero)
    __set_FPSCR(fpscr);
#endif
}



// === 공개 API: UI부(main.c)에서 호출 ===
void notch_init(uint32_t fs_hz)
{

    g_fs_hz = fs_hz;                 // ★ 샘플레이트 저장 (IIR 설계용)
    dsp_enable_ftz_dn();             // ★ 서브노멀 완전 차단 (1회)

    (void)fs_hz; // 추후 필터 설계 시 사용 가능


    // 첫 출력 버퍼는 0으로 클리어
    for (uint32_t i = 0; i < BLOCK_SIZE_U16 * 2; ++i) txBuf[i] = 0;

    // IIR 초기 파라미터 반영
    g_prev_on = 0xFF;                // 강제 리디자인 트리거
    notch_iir_update_if_needed();
    ps_init(g_fs_hz);
    sg_init(g_fs_hz);   // ★ SOUND GEN 초기화 (샘플레이트 기반)
    Tuner_Init(g_fs_hz);
    met_click_build_tables((float)hi2s2.Init.AudioFreq);

    note_set_fs((float)hi2s2.Init.AudioFreq);

    cb_state = 0;
}

HAL_StatusTypeDef notch_start(void)
{
    // ★ 마이크 버퍼 클리어 & I2S3 RX DMA 시작 (이미 배선/IOC 완료 가정)
    memset(mic3RxBuf, 0, sizeof(mic3RxBuf));
    (void)HAL_I2S_DMAStop(&hi2s3); // 안전 정지
    if (HAL_I2S_Receive_DMA(&hi2s3, (uint16_t*)mic3RxBuf, BLOCK_SIZE_U16) == HAL_OK) {
        mic3_running = 1;
    } else {
        mic3_running = 0; // 마이크 미동작 시에도 I2S2는 정상 진행
    }

    // I2S2 Full-Duplex: txBuf→DAC, rxBuf←ADC. (기존 코드 유지)
    return HAL_I2SEx_TransmitReceive_DMA(&hi2s2, txBuf, rxBuf, BLOCK_SIZE_U16);
}

void notch_stop(void)
{
    // ★ I2S3 RX 정지 (마이크 먼저 멈춤)
    (void)HAL_I2S_DMAStop(&hi2s3);
    mic3_running = 0;

    // (기존 코드 유지)
    (void)HAL_I2SEx_TransmitReceive_DMA(&hi2s2, NULL, NULL, 0); // no-op safety
    (void)HAL_I2S_DMAStop(&hi2s2);
    cb_state = 0;
}


void notch_task(void)
{
    // DMA 콜백에서 이미 반버퍼 즉시 처리하므로 폴링 태스크는 비움
	//Tuner_Task();
    cb_state = 0;
}
