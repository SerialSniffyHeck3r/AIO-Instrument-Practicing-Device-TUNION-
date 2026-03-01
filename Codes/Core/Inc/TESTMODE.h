#ifndef TESTMODE_H
#define TESTMODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 부팅 직후 E3 길게 눌림 감지 결과
extern volatile uint8_t TestModeEnabled;

// 부팅 시 E3(액티브-로우) 1초 홀드 → TestModeEnabled=1 + 토스트
void BootCheck_TestMode(void);

// TEST MODE 전용 메인 루프(복귀 없음: 재부팅으로만 탈출)
void TestMode_RunLoop(void);

#ifdef __cplusplus
}
#endif

#endif // TESTMODE_H
