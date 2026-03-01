#ifndef UI_STATE_H
#define UI_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

// 공용 UI 상태 enum (main.c에서 쓰던 그대로 옮김)
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

    UI_STATE_COUNT
} UIState;

// 공용 현재 상태 변수 선언(선언만!)
extern UIState currentUIState;


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

#ifdef __cplusplus
}
#endif
#endif // UI_STATE_H
