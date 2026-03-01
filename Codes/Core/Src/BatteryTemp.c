#include "BatteryTemp.h"
#include "main.h"           // HAL_GetTick, 전역 extern들
#include "LCD16X2.h"
#include "LCD16X2_cfg.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "dht.h"              // ★ 추가


/* ──────────────────────────────────────────────────────────────
 * degree[] 외부참조 설정
 *  - 권장: main.c의 degree[]에서 static 제거 → extern 사용
 *  - 대안: 아래 매크로 주석 처리 → 로컬 폴백 패턴 사용
 * ────────────────────────────────────────────────────────────── */
#define BATTEMP_USE_EXTERN_DEGREE 1
#ifndef BATTTEMP_DEG_SLOT
#define BATTTEMP_DEG_SLOT 0u
#endif

// ==== DHT22 on PA9, TIM7(us) ====
#define DHT_GPIO_Port   GPIOE
#define DHT_Pin         GPIO_PIN_14

#define MyLCD LCD16X2_1
static DHT_t g_dht;



/* ──────────────────────────────────────────────────────────────
 * 외부 전역값 (main.c에 존재)
 * ────────────────────────────────────────────────────────────── */
extern volatile uint8_t FlashDebug;   // 1이면 텍스트(mV/%), 0이면 그래픽바  :contentReference[oaicite:6]{index=6}
extern uint16_t MainBattADC;          // 배터리 ADC 0..4095               :contentReference[oaicite:7]{index=7}
extern uint16_t Temperature;          // 0.1°C 단위                        :contentReference[oaicite:8]{index=8}
extern uint16_t Humidity;             // 0..99 %                           :contentReference[oaicite:9]{index=9}

extern uint8_t g_dht_req;
extern TIM_HandleTypeDef htim7;   // ★ TIM7 핸들 (tim.c에서 선언)

extern volatile uint8_t TempUnitF;   // 0=C, 1=F
extern volatile uint8_t DHT22_FAIL;  // 1=최근 측정 실패/이상

extern const uint8_t degree[8];  // ← main.c의 degree[] (권장)
extern uint8_t ModeSelectionFirstRunFlag;

extern volatile float Cal_TempC_Offset, Cal_TempC_Scale;
extern volatile float Cal_RH_Offset,   Cal_RH_Scale;
/* ──────────────────────────────────────────────────────────────
 * 사용자 조정 파라미터
 * ────────────────────────────────────────────────────────────── */
#ifndef BATTTEMP_DEFAULT_HOLD_MS
#define BATTTEMP_DEFAULT_HOLD_MS   3000u    // 팝업 유지시간 기본 3초
#endif

#ifndef BATTTEMP_REFRESH_MS
#define BATTTEMP_REFRESH_MS         1000u    // LCD 리프레시 주기(논블로킹)
#endif

#ifndef BATTEMP_HDR_INTERVAL_MS
#define BATTEMP_HDR_INTERVAL_MS         1000u    // LCD 리프레시 주기(논블로킹)
#endif


#ifndef BATTTEMP_DEG_SLOT
#define BATTTEMP_DEG_SLOT             0u    // CGRAM 슬롯(0~7) — ModeSelection에선 0번이 비는 편
#endif



#ifndef BATTTEMP_DEFAULT_HOLD_MS
#define BATTTEMP_DEFAULT_HOLD_MS   3000u
#endif
#ifndef BATTTEMP_REFRESH_MS
#define BATTTEMP_REFRESH_MS         100u
#endif

// ===== 설정(필요 시 숫자만 바꿔도 됨) =====
#ifndef BATTLOW_THRESHOLD_MV
#define BATTLOW_THRESHOLD_MV     3200u   // 저전압 임계(mV) — 보드에 맞게 조정
#endif
#ifndef BATTLOW_CHECK_PERIOD_MS
#define BATTLOW_CHECK_PERIOD_MS   100u   // 감시 주기
#endif
#ifndef BATTLOW_FLASH_MS
#define BATTLOW_FLASH_MS          300u   // "LOW" 경고 시 빨강 유지 시간
#endif
#ifndef BATTLOW_EXIT_OK_HYST_MS
#define BATTLOW_EXIT_OK_HYST_MS  1000u   // 임계 이상 유지해야 통과하는 시간
#endif
#ifndef POWER_ON_DELAY_MS
#define POWER_ON_DELAY_MS        3000u   // 통과 후 전원 켜기까지 대기(3초)
#endif
/* ──────────────────────────────────────────────────────────────
 * 로컬 상태
 * ────────────────────────────────────────────────────────────── */
static uint8_t  s_active      = 0;
static uint32_t s_end_ms      = 0;
static uint32_t s_next_ms     = 0;
static uint32_t s_hold_ms     = BATTTEMP_DEFAULT_HOLD_MS;

static uint32_t s_batt_hold_ms  = BATTTEMP_DEFAULT_HOLD_MS;






// DHT22 드라이버에 따라 바뀔 것.
//extern int DHT22_ReadBlocking(float *outTempC, float *outRH);

// 네 전역과 포맷 규칙에 맞춤(0.1°C, 0..99%)
static inline uint16_t to_tenth_deg(float tC) {
    if (tC < 0) tC = 0; if (tC > 99.9f) tC = 99.9f;
    return (uint16_t)(tC * 10.0f + 0.5f);
}
static inline uint16_t to_rh_uint(float rh) {
    if (rh < 0) rh = 0; if (rh > 99.0f) rh = 99.0f;  // 100% 금지
    return (uint16_t)(rh + 0.5f);
}
/* ──────────────────────────────────────────────────────────────
 * DHT22
 * ────────────────────────────────────────────────────────────── */

// main에서 호출: DHT 하드 초기화
void BatteryTemp_DHT_Init(void)
{
    // APB1 Timer = 84MHz → timerBusFrequencyMHz = 84
    DHT_init(&g_dht, DHT_Type_DHT22, &htim7, 84, DHT_GPIO_Port, DHT_Pin);
}

// main의 HAL_GPIO_EXTI_Callback에서 호출해줄 포워더
void BatteryTemp_OnExti(uint16_t pin)
{
    if (pin == DHT_Pin) {
        DHT_pinChangeCallBack(&g_dht);
    }
}

// 블로킹 1회 읽기 래퍼 (성공=0, 실패=-1)
static int DHT22_ReadBlocking(float *outTempC, float *outRH)
{
    float t = 0.0f, h = 0.0f;
    if (!DHT_readData(&g_dht, &t, &h)) return -1;  // false면 실패
    if (outTempC) *outTempC = t;
    if (outRH)    *outRH    = h;
    return 0;
}












// 파일: BatteryTemp.c
void Measure_Service(void)
{
    if (!g_dht_req) return;
    g_dht_req = 0;



    // ── 재시도/디바운스 파라미터 ──────────────────────
    #ifndef DHT_READ_RETRIES
    #define DHT_READ_RETRIES          8      // 시도 횟수
    #endif
    #ifndef DHT_READ_RETRY_DELAY_MS
    #define DHT_READ_RETRY_DELAY_MS   10u    // 시도 간 대기(ms)
    #endif
    #ifndef DHT_FAIL_DEBOUNCE_COUNT
    #define DHT_FAIL_DEBOUNCE_COUNT   5      // 이 횟수 연속 실패 시 FAIL 표시
    #endif
    static uint8_t s_fail_consec = 0;
    // ────────────────────────────────────────────────

    float tC_raw = 0.0f, rh_raw = 0.0f;
    uint8_t ok = 0;

    for (int attempt = 0; attempt < DHT_READ_RETRIES; ++attempt) {
        if (DHT22_ReadBlocking(&tC_raw, &rh_raw) == 0) {
            // ① raw 유효성 검사(센서 명세 보수 범위)
            if (tC_raw >= -40.0f && tC_raw <= 80.0f &&
                rh_raw >=   0.0f && rh_raw <= 100.0f) {
                ok = 1;
                break;
            }
        }
        // 실패 시: 잠깐 쉬고 다시 시도 (ISR 금지 구역 아님)
        HAL_Delay(DHT_READ_RETRY_DELAY_MS);
    }

    if (ok) {
        // ② 보정 적용 (scale → offset)
        float tC_corr = tC_raw * Cal_TempC_Scale + Cal_TempC_Offset;
        float rh_corr = rh_raw * Cal_RH_Scale   + Cal_RH_Offset;

        DHT22_FAIL   = 0;
        s_fail_consec= 0;

        // ③ 전역 저장(표시 스펙 클램프는 기존 헬퍼 사용)
        Temperature = to_tenth_deg(tC_corr);  // 0.1°C
        Humidity    = to_rh_uint(rh_corr);    // 0..99%
    } else {
        // 이 사이클은 실패로 기록
        if (++s_fail_consec >= DHT_FAIL_DEBOUNCE_COUNT) {
            DHT22_FAIL = 1;   // 2사이클 연속 실패 시에만 화면에 '--' 노출
        }
        // 저장 값은 건드리지 않음(부분 갱신 로직이 숫자칸을 '-'로 덮음)
    }
}




/* ──────────────────────────────────────────────────────────────
 * 헬퍼: 안전 클램프
 * ────────────────────────────────────────────────────────────── */
static inline uint16_t clamp_u16_int(int v, int lo, int hi)
{
    if (v < lo) return (uint16_t)lo;
    if (v > hi) return (uint16_t)hi;
    return (uint16_t)v;
}

static uint32_t s_hdr_interval_ms = 100u;
void BatteryTemp_HeaderSetInterval(uint32_t ms) {
    s_hdr_interval_ms = (ms == 0u) ? 1u : ms;
}

/* ──────────────────────────────────────────────────────────────
 * 배터리 변환 (간단 스케일)
 *  - ADC: 12bit(0..4095), Vref=3.3V → mV 환산
 *  - 퍼센트: 단순 비례(캘리브레이션 필요시 맵핑 바꿔도 됨)
 * ────────────────────────────────────────────────────────────── */
static inline uint16_t adc_to_mv(uint16_t adc)
{
    // (adc * 3300) / 4095, 반올림 보정
    return (uint16_t)(( (uint32_t)adc * 3300u + 2047u ) / 4095u);
}
static inline uint8_t adc_to_pct(uint16_t adc)
{
    if (adc >= 4095u) return 100u;
    return (uint8_t)(( (uint32_t)adc * 100u ) / 4095u);
}

/* ──────────────────────────────────────────────────────────────
 * 2행 그래픽 바:  E>>>>>>>>>>>>>>F  (폭=14칸)
 * ────────────────────────────────────────────────────────────── */

/* ──────────────────────────────────────────────────────────────
 * 2행 텍스트: "V:xxxxmV  yy%"
 * ────────────────────────────────────────────────────────────── */

/* ──────────────────────────────────────────────────────────────
 * 1행: "T:xx.x C H:yy%"
 *  - degree 커스텀문자를 'C' 바로 앞 칸(원래 공백)에 오버레이
 * ────────────────────────────────────────────────────────────── */
static void draw_temp_humi_row1(void)
{
    char l1[17];

    if (DHT22_FAIL) {
        // 예외: 센서 실패/이상 → 숫자 대신 대시 출력
        // 형식: "--.-  --%" (16칸에 맞춰 좌측부터)
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "                ");
        LCD16X2_Set_Cursor(MyLCD, 1, 1);
        LCD16X2_Write_String(MyLCD, "--.-  --%");
        // degree 오버레이는 생략(C/F 문자가 없으므로)
        return;
    }

    // 정상: 내부 보관은 0.1°C. 표시 단위 변환만 수행.
    uint16_t t01_c = Temperature;                 // 0.1°C
    float    tC    = (float)t01_c / 10.0f;
    float    tDisp = tC;
    char     unit  = 'C';

    if (TempUnitF) {
        // 화씨 변환: F = C*9/5 + 32, 표시폭 보호 위해 0.0~99.9로 클램프
        float tF = (tC * 9.0f / 5.0f) + 32.0f;
        if (tF < 0.0f)   tF = 0.0f;
        if (tF > 99.9f)  tF = 99.9f;
        tDisp = tF;
        unit  = 'F';
    } else {
        // 섭씨도 LCD 폭 보호(0.0~99.9)
        if (tDisp < 0.0f)   tDisp = 0.0f;
        if (tDisp > 99.9f)  tDisp = 99.9f;
    }

    int t_int = (int)(tDisp + 0.0001f);           // 정수부
    int t_fr1 = (int)((tDisp - (float)t_int) * 10.0f + 0.5f); // 소수1자리 반올림

    uint16_t h_pct = clamp_u16_int((int)Humidity, 0, 99);

    // "xx.xC yy%" (총 11칸 정도). 기존처럼 한 줄 클리어 후 재작성.
    // 필요 시 앞에 라벨 안 쓰고 숫자부분만 갱신하는 기존 흐름을 유지.
    snprintf(l1, sizeof(l1), "%2d.%1d%c %02u%%", t_int, t_fr1, unit, (unsigned)h_pct);

    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "                ");
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, l1);

    // 단위 문자(C/F) 앞 칸에 degree 커스텀문자 오버레이
    char* pU = strchr(l1, unit);
    if (pU) {
        int idxU    = (int)(pU - l1);       // 0-based index within l1
        int col_deg = idxU;                 // 단위 문자 앞 칸(문자열 내 공백)에 덮어씀
        if (col_deg > 0 && col_deg < 16) {
            LCD16X2_DisplayCustomChar(MyLCD, 1, (uint8_t)(col_deg + 1), BATTTEMP_DEG_SLOT);
        }
    }
}

/* ──────────────────────────────────────────────────────────────
 * 퍼블릭: 팝업 유지시간 변경
 * ────────────────────────────────────────────────────────────── */
void BatteryTemp_SetHoldMs(uint32_t ms)
{
    s_hold_ms = (ms == 0u) ? BATTTEMP_DEFAULT_HOLD_MS : ms;
}

static inline void Battery_SetHoldMs(uint32_t ms) {
    s_batt_hold_ms = (ms == 0u) ? 1u : ms;
}


/* ──────────────────────────────────────────────────────────────
 * 퍼블릭: 팝업 활성 여부
 * ────────────────────────────────────────────────────────────── */
uint8_t BatteryTemp_IsActive(void)
{
    return s_active;
}



/* ──────────────────────────────────────────────────────────────
 * 메인 API: 팝업 표시 (논블로킹)
 *  - 처음 호출 시: degree 커스텀문자 등록, 만료시간 셋업
 *  - 이후 호출 시: 주기적으로 화면 업데이트, 만료되면 자동 종료
 *  - FlashDebug==1 → 2행 텍스트(mV & %)
 *    FlashDebug==0 → 2행 그래픽바만
 * ────────────────────────────────────────────────────────────── */
void BatteryTemp_ShowPopupLCD(void)
{
    uint32_t now = HAL_GetTick();

    if (!s_active) {
        s_active  = 1;
        s_end_ms  = now + s_hold_ms;
        s_next_ms = 0;

        // degree 커스텀 캐릭터 등록 (슬롯 BATTTEMP_DEG_SLOT에)
        LCD16X2_RegisterCustomChar(MyLCD, BATTTEMP_DEG_SLOT, (uint8_t*)degree);

        // 첫 그리기 즉시
        s_next_ms = now;
    }

    if (now >= s_next_ms) {
        // 1행: 온/습 + degree
        draw_temp_humi_row1();

        // 2행: 배터리
        uint16_t mv  = adc_to_mv(MainBattADC);
        uint8_t  pct = adc_to_pct(MainBattADC);
        if (FlashDebug) {
            draw_batt_text_row2(mv, pct);
        } else {
            draw_batt_graphic_row2(pct);
        }

        s_next_ms = now + BATTTEMP_REFRESH_MS;
    }

    // 만료 처리
    if ((int32_t)(now - s_end_ms) >= 0) {
        s_active = 0;
        // 팝업 종료 후 다음에 다시 들어오면 첫 호출 분기에서 자동 재등록/표시됨
    }
}


void BatteryTemp_DrawStaticHeader(void)
{
    // 커스텀 ° 한 번만 등록
    static uint8_t s_deg_loaded = 0u;
    if (!s_deg_loaded) {
        LCD16X2_RegisterCustomChar(MyLCD, BATTTEMP_DEG_SLOT, (uint8_t*)degree);
        s_deg_loaded = 1u;
    }

    // "00.0 C 00%"를 먼저 쓰고, 'C' 앞 칸에 ° 커스텀 문자 덮어쓰기
    // 1) 전체 라인을 깔끔히 정리(16칸 맞춰 공백 패딩)
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "                ");

    // 2) 기본 문자열 출력 (° 자리는 일단 공백)
    //    인덱스(1-based): 1 2 3 4 5 6 7 8 9 10 ...
    //                     0 0 . 0 _ C _ 0 0 % ...
    //           → 5번째 칸에 ° 를 덮어놓을 것
    LCD16X2_Set_Cursor(MyLCD, 1, 1);
    LCD16X2_Write_String(MyLCD, "00.0 C 00%");

    // 3) ° 커스텀 문자 오버레이 (row=1, col=5)
    LCD16X2_DisplayCustomChar(MyLCD, 1, 5, BATTTEMP_DEG_SLOT);
}


//==============================================
// ==== HeaderService: 기존 변경-감지 갱신 + 복귀 1회 강제 재도색 ====
#ifndef BATTEMP_HDR_INTERVAL_MS
#define BATTEMP_HDR_INTERVAL_MS 100u
#endif
#ifndef BATTTEMP_DEG_SLOT
#define BATTTEMP_DEG_SLOT 0u
#endif

extern const uint8_t degree[8];
extern uint16_t Temperature;          // 0.1°C
extern uint16_t Humidity;             // 0..99

// 외부에서(예: Save Process 이후) 강제 1회 복원을 트리거
static volatile uint8_t s_hdr_force_restore = 1u; // 부팅 첫 진입 시에도 1회 표시되게

void BatteryTemp_HeaderMarkDirty(void) { s_hdr_force_restore = 1u; }

// (로컬) 마지막으로 LCD에 쓴 문자 캐시
static char p1 = (char)0xFF;  // 1: 온도 10의 자리(' ' 가능)
static char p2 = (char)0xFF;  // 2: 온도 1의 자리
static char p4 = (char)0xFF;  // 4: 온도 0.1 자리
static char p7 = (char)0xFF;  // 7: 습도 10의 자리(' ' 가능)
static char p8 = (char)0xFF;  // 8: 습도 1의 자리




void BatteryTemp_HeaderService(void)
{
	uint16_t h = clamp_u16_int((int)Humidity, 0, 99);

	char c1, c2, c4, c7, c8;

	if (DHT22_FAIL) {
	    // 실패 시: 숫자 칸만 대시로. 고정 기호(., degree, 공백, %)는 기존 유지.
	    c1 = '-'; c2 = '-'; c4 = '-';
	    c7 = '-'; c8 = '-';
	} else {
	    // 온도: 보관은 0.1°C. 표시단위(℃/℉)만 변환 + LCD 폭 보호(0.0~99.9)
	    float tC    = (float)Temperature / 10.0f;
	    float tDisp = tC;
	    if (TempUnitF) {
	        tDisp = (tC * 9.0f / 5.0f) + 32.0f;
	    }
	    if (tDisp < 0.0f)  tDisp = 0.0f;
	    if (tDisp > 99.9f) tDisp = 99.9f;

	    uint16_t ti  = (uint16_t)tDisp;                                  // 정수부
	    uint16_t tf1 = (uint16_t)((tDisp - (float)ti) * 10.0f + 0.5f);    // 소수1자리(반올림)

	    c1 = (ti >= 10u) ? (char)('0' + ((ti / 10u) % 10u)) : ' ';
	    c2 = (char)('0' + (ti % 10u));
	    c4 = (char)('0' + (tf1 % 10u));

	    c7 = (h >= 10u) ? (char)('0' + ((h / 10u) % 10u)) : ' ';
	    c8 = (char)('0' + (h % 10u));
	}


    // ── 복귀/Dirty: "무조건 1회" 숫자칸 찍기 + 고정기호 복원 ──
    if (s_hdr_force_restore) {
        s_hdr_force_restore = 0u;

        // ° 재등록 + 고정기호(3,5,6,9) 1회 오버레이 (라인 클리어 금지)
        LCD16X2_RegisterCustomChar(MyLCD, BATTTEMP_DEG_SLOT, (uint8_t*)degree);
        LCD16X2_Set_Cursor(MyLCD, 1, 3); LCD16X2_Write_Char(MyLCD, '.');
        LCD16X2_DisplayCustomChar(MyLCD, 1, 5, BATTTEMP_DEG_SLOT);
        LCD16X2_Set_Cursor(MyLCD, 1, 6); LCD16X2_Write_Char(MyLCD, ' ');
        LCD16X2_Set_Cursor(MyLCD, 1, 9); LCD16X2_Write_Char(MyLCD, '%');

        // ★ 비교 없이 '현재값'을 숫자칸에 강제 1회 쓰기 (빈칸 복원 보장)
        LCD16X2_Set_Cursor(MyLCD, 1, 1); LCD16X2_Write_Char(MyLCD, c1); p1 = c1;
        LCD16X2_Set_Cursor(MyLCD, 1, 2); LCD16X2_Write_Char(MyLCD, c2); p2 = c2;
        LCD16X2_Set_Cursor(MyLCD, 1, 4); LCD16X2_Write_Char(MyLCD, c4); p4 = c4;
        LCD16X2_Set_Cursor(MyLCD, 1, 7); LCD16X2_Write_Char(MyLCD, c7); p7 = c7;
        LCD16X2_Set_Cursor(MyLCD, 1, 8); LCD16X2_Write_Char(MyLCD, c8); p8 = c8;

        return; // 1회 복원 끝. 이후로는 변경 시에만 갱신
    }

    // ── 평상시: 변경된 숫자칸만 갱신(깜빡임 0) ──
    if (c1 != p1) { LCD16X2_Set_Cursor(MyLCD, 1, 1); LCD16X2_Write_Char(MyLCD, c1); p1 = c1; }
    if (c2 != p2) { LCD16X2_Set_Cursor(MyLCD, 1, 2); LCD16X2_Write_Char(MyLCD, c2); p2 = c2; }
    if (c4 != p4) { LCD16X2_Set_Cursor(MyLCD, 1, 4); LCD16X2_Write_Char(MyLCD, c4); p4 = c4; }
    if (c7 != p7) { LCD16X2_Set_Cursor(MyLCD, 1, 7); LCD16X2_Write_Char(MyLCD, c7); p7 = c7; }
    if (c8 != p8) { LCD16X2_Set_Cursor(MyLCD, 1, 8); LCD16X2_Write_Char(MyLCD, c8); p8 = c8; }
}



