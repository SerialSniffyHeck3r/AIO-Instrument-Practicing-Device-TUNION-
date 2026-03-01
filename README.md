# STM32F4 Portable Practice DSP Device

This was developed for my bachelor graduation project, but with keeping in mind that the device will be used personally or crowdfunded.
It turned out it wasn't that good to be crowdfunded.. but still it works! and also good for my personal use.

It is basically a **portable practice companion** built around an **STM32F407** microcontroller.  
The device helps me practice instruments at home or in a rehearsal room without a lot of external gear.

## TL;DR (for recruiters)

- **Platform:** STM32F407, dual I2S audio, RGB 16x2 LCD, LED bar, rotary encoder, Li-ion battery
- **Focus:** Real-time audio DSP for instrument practice (notch filter, pitch shift, mixer), tuner, metronome
- **Reliability:** Flash config storage with CRC and wear-leveling, backup SRAM snapshot, factory self-test mode
- **Result:** Fully working handheld prototype with custom PCB, firmware, and UX


## STM32F4 기반 악기 연습 장치 'Tunion': 한국어 설명

해당 레포지토리는 본인의 졸업 작품으로써 설게한 제품에 대해 다루고 있습니다.

가정 및 연습실 환경에서는 악기를 크게 연주할 수 없어 뮤트를 착용하거나 전자 악기를 연주하게 된다는 문제 의식에서 출발하였으며, 악기 케이스에 항상 휴대하고 다닐 수 있으면서 악기를 연주할 때 바로 꺼내서 사용할 수 있는 만능 장비를 목표로 개발되었습니다. 
- 반주를 듣고, 반주가 없을 때 아무 음원이나 틀어서 Notch Filter를 적용하여 본인이 연주하는 악기 대역만 잘라서 그 부분과 본인의 악기 소리를 같이 들으면서 연습하거나
- 비트 및 서브비트 등 다양한 기능이 포함된 메트로놈 소리를 들으면서 반주도 같이 듣고 그 소리에 맞춰 악기를 연습하거나
- 악기를 조율하거나
- 또는 악기를 적절한 온습도의 환경에서 보관할 수 있게 하는 것

을 목표로 두고 있습니다.

본 기기의 기능은 다음과 같습니다:
- 악기로부터 들어오는 악기 소리 아날로그 입력 / 음향기기로부터 들어오는 반주 입력을 믹싱 및 개별 볼륨 조절
- 반주에서 본인이 연주하고자 하는 악기 대역을 자를 수 있는 Notch Filter를 적용해서 악기 소리와 믹싱
- HSE를 통해 달성한 고정밀 메트로놈, Sub-Beat, Tap Tempo, 리타르단도 기능을 포함하는 시중에도 찾아보기 힘든 다기능 메트로놈
- 악기 튜너 및 펑션 제너레이터
- 환경설정 및 Flash와 백업 SRAM에 환경설정 다중 백업
- Real-Time Clock, 연습 시간 기록 기능
- 실제 생산을 위한 하드웨어 셀프 테스트 (RAM, Flash 접근 시간 테스트, LCD 및 오디오 출력 테스트)
- 스스로의 펌웨어를 알아서 '죽이는' 히든 커맨드


### 기기 내부 저장 방식

본인을 레트로 컴퓨팅에 관심이 많으며, 레트로 컴퓨팅을 연구하거나 임베디드 시스템을 개인적으로 연구하면서 생긴 아이디어를 적용했습니다.

MCU와 내부 RAM이 살아 있는 상황에서
- Flash에 저장하는 경우: 홈 화면으로 복귀 / 절전 모드에서 탈출했을 때 설정 값이 이전과 변화했을 경우에만 Write
- BKPSRAM에 저장하는 경우: 모든 버튼이 조작되었을 때. Overhead가 낮아 최대 1초에 30번을 조작해도 시스템의 동작 안정성을 저해하지 않는다는 판단.

전원이 완전히 차단되었으나 백업 전지는 살아 있는 경우:
- 전원 인가시 BKPSRAM의 값 확인 후, Magic Number (0xDEADBEEF) Check
- 백업 전지가 살아 있으므로 Magic Number가 그대로 있으므로 유효하다고 판단하여, BKPSRAM에서 바로 값을 불러옴

전원이 완전히 차단되고 백업 배터리까지 빠졌을 경우:
- 전원 인가시 BKPSRAM의 값 확인 후 Magic Number가 유효하지 않음을 확인
- 사용자에게 Recover Data? YES / NO 인터페이스를 통해, 백업 배터리 분리가 초기화를 위한 의도적 행동인지 확인.
- 만약 YES를 선택했을 경우, FLASH에서 값을 복원. NO를 선택했을 경우 시스템 설정이 출고 시와 동일하게 초기화됨.
- 어차피 백업 배터리가 분리되었으면 **당연히** RTC도 유효하지 않을 것이므로, Yes / No 선택 유무에 상관없이 시계 재설정 유도
- 이후 정상 부팅 



---


---

## Backstory

I play lots of instrument from woodwinds to percussion. Music is pretty much literally my life. 
When I practice guitar or other instruments at home, 
I usually need many separate devices: audio interface, mixer, tuner, metronome, maybe a laptop? 

I wanted **one small box** that can do all of this, and this is the begin of this big project.
This is a graduation project, but my intention was to make something that is actually useful and is potentially lucrative.

The function that I planned to implement was:
- mix backing track and my instrument,
- remove some frequency range (for example vocals),
- quickly transpose the song,
- show audio level
- tuner and metromone with lots of features
- features that every portable embedded device that is on sale should have

So I designed and built this device as my **bachelor graduation project**.

---

## What this device does

From a user point of view, this box is:

- A **small mixer** for two stereo audio sources
- A **practice helper** that can cut or change a specific frequency band, then mix audio with my instrument sound
- A **digital tuner** with 1 cent resolution
- A **metronome** with different time signatures and tempo changes
- A **status monitor** for battery, temperature, humidity and system state

All of this runs on one STM32F407 MCU with no external DSP chip.

---

## Main features

### 1. Real-time audio DSP pipeline

- Two I2S inputs (for example: backing track + instrument/mic)
- DMA double-buffered ring pipeline (16-bit audio → float → processed → 16-bit)
- **Real-time mixing** of both channels with:
  - 50-step digital volume for each channel
  - master volume and balance
  - dBFS level meters for input level check

### 2. Adjustable band-cut / notch filter for practice

- User can select a **frequency range to cut** (for example vocal range)
- Implemented with **ARM CMSIS-DSP IIR biquad filters**
- Supports:
  - on/off control from the UI
  - start / end frequency
  - **three user presets** per instrument (saved in non-volatile memory)

This lets me listen to a song while removing or reducing the part I want to practice over.

### 3. Simple pitch shift for fast transposition

- Integrated **pitch shift** engine for quick transpose
- Control in **semitones** (for example −3…+3)
- Focus is on **low latency** and “good enough for practice”,  
  not studio-quality offline processing

### 4. Integrated tuner

- **Digital chromatic tuner** with 1-cent resolution
- AMDF-based pitch detection with:
  - harmonic / octave correction
  - pre-lock and lock logic to avoid jumping notes
- Adjustable:
  - **A4 base frequency** (calibration)
  - input sensitivity

The tuner drives both the LCD and the LED bar for visual feedback.

### 5. Sample-accurate metronome & tone generator

- Metronome audio is generated **inside the same DSP pipeline** as the audio
- Features:
  - accents and sub-clicks
  - tempo changes (ritardando / accelerando / back to a-tempo)
  - separate metronome volume
- Extra **sound generator mode** (sine / square / triangle)  
  for tuning or quick tests.

### 6. User interface and UX

- **RGB 16x2 character LCD** (HD44780-compatible)
  - custom UI pages for each mode (practice, tuner, metronome, settings)
- **LED bar** driven by PWM:
  - VU meter for input / mic levels
  - tuner center indication
  - metronome visualization
- **Rotary encoder + buttons**:
  - used for navigation and parameter editing
  - single short/long click design so it can be used while holding an instrument

I tried to keep the UI simple enough to use with one hand.

### 7. Power, environment and settings

- Powered from a **single Li-ion battery**
- Battery:
  - voltage measurement and level estimation
  - low-battery handling
- **Temperature & humidity** sensor (DHT22):
  - shows instrument storage environment
- **Config storage:**
  - main storage in Flash (sector with custom format)
  - record format with version, sequence number and CRC32
  - background service that only writes when values changed
- **Backup snapshot:**
  - second copy in backup SRAM,  
    so the device can restore settings quickly after a battery change

### 8. Factory / test mode

- Hidden button combo opens a **self-test mode**
- Used for:
  - LCD and LED test
  - audio path test
  - peripheral checks
- This was designed with **future production / repair** in mind.

---

## Hardware overview

- MCU: **STM32F407** (Cortex-M4 with FPU and DSP extensions)
- Audio:
  - Dual I2S interface
  - 16-bit stereo in/out
- UI:
  - HD44780-compatible RGB 16x2 LCD
  - 10-segment (or similar) LED bar
  - rotary encoder + buttons
- Sensors:
  - DHT22 temperature / humidity
  - battery voltage measurement
- Power:
  - Li-ion battery
  - backup power for RTC and SRAM

Custom PCB was designed so that the device is actually **hand-held and portable**. 

---

## Firmware / code structure (short version)

The firmware is written in **C** using the STM32 HAL and CMSIS-DSP.

- `main.c`  
  - main UI state machine, mode handling, LCD updates
- `notch.c`  
  - audio DSP pipeline, I2S callback, mixing, notch filter, pitch shift, metronome, sound generator
- `tuner.c`  
  - pitch detection and note / cent calculation
- `LEDcontrol.c`  
  - LED bar effects, VU meter and tuner / metronome visualization
- `BatteryTemp.c`, `dht.c`  
  - battery and DHT22 handling
- `PowerFlash.c`  
  - Flash config storage with CRC and wear-leveling
- `PowerSave.c`  
  - backup SRAM snapshot for fast resume
- `Clock.c`  
  - RTC time setting UI

The code is clearly not perfect, as this is one of my first huge system architect project.
but it is a complete prototype that I could really use as my **daily practice companion**.
