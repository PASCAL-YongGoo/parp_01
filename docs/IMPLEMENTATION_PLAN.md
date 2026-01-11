# PARP-01 Application Feature Implementation Plan

> **Last Updated**: 2026-01-11
> **Current Status**: Hardware fully configured, minimal software implementation (LED blink only)

---

## Table of Contents
1. [Current Project Status](#current-project-state)
2. [Available Hardware Resources](#available-hardware-resources)
3. [Implementation Phases](#implementation-phases)
4. [Detailed Feature Plans](#detailed-feature-plans)
5. [Testing Strategy](#testing-strategy)
6. [Memory Budget](#memory-budget)

---

## Current Project Status

### Hardware Configuration: ✅ EXCELLENT (95% Complete)
- **Custom Board Definition**: Complete DTS with all peripherals configured
- **Clock System**: 275 MHz SYSCLK (optimized and safe)
- **Pin Configuration**: All peripherals mapped and validated
- **Build System**: Fully functional and tested

### Software Implementation: MINIMAL (10%)
- **Current**: Only LED blink demo on PE6
- **Unused Hardware**: 90% of defined peripherals
- **Application Features**: Minimal

### Available Hardware Resources

Based on the device tree analysis, the following hardware is defined and ready:

**GPIO:**
- ✅ 3x LEDs (PE2, PE3, PE6) - GPIO active-high
- ✅ 2x Switches (PD10, PD11) - GPIO active-low with pull-up

**Communication Interfaces:**
- ✅ USART1 (PB14-TX, PB15-RX) - 115200 baud, TX/RX swapped - **Currently used for console**
- ✅ UART4 (PD0-TX, PD1-RX) - Available, unused
- ✅ UART5 (PB5-TX, PB6-RX) - Available, unused
- ✅ I2C4 (PB7-SDA, PB8-SCL) with M24C64 EEPROM @ 0x50
- ✅ USB OTG HS (PA11/PA12)

**Peripherals Defined**:
- ✅ 3 LEDs (PE2, PE3, PE6)
- ✅ 2 Switches (PD10, PD11 - active low with pull-up)
- ✅ I2C4 bus with M24C64 EEPROM (8KB, address 0x50)
- ✅ USB OTG HS
- ✅ Multiple UARTs (USART1, UART4, UART5)
- ✅ RTC with LSE clock
- ✅ RNG (Random Number Generator)
- ✅ Backup SRAM

**현재 구현 상태**: LED 점멸 데모만 구현됨 (하드웨어의 약 10%만 활용)

## 기능 구현 계획

제가 세 가지 단계의 포괄적인 계획을 준비했습니다. 어떤 우선순위로 구현하고 싶으신지 알려주시면 구체적인 계획을 세우겠습니다.

### **현재 프로젝트 상태**
- ✅ **하드웨어 설정**: 완벽하게 정의됨 (95% 완료)
- ⚠️ **소프트웨어 구현**: 최소한의 LED 깜빡임만 구현 (10% 완료)
- ✅ **빌드 시스템**: 완전히 작동
- ⚠️ **하드웨어 테스트**: 아직 실제 보드에서 테스트 안 됨

---

## 📋 PARP-01 기능 구현 계획

### 현재 상태 요약

**하드웨어 준비 상태: 95% ✅**
- STM32H723ZG 기반 커스텀 보드
- 275 MHz 시스템 클럭 (안전 최대값)
- 모든 주변장치 DTS에 정의됨
- 3개 LED, 2개 스위치, I2C EEPROM, USB, RTC 등 준비 완료

**소프트웨어 구현 상태: 10% ⚠️**
- 현재: LED 깜빡임만 구현됨
- 정의된 하드웨어의 90%가 미사용 상태

---

## 📋 기능 구현 로드맵

### **Phase 1: 기본 I/O 기능 (우선순위: 높음)**
현재 LED blink 데모만 있으므로, 기본적인 입출력 기능을 먼저 완성합니다.

#### 1.1 스위치 입력 처리
- **목표**: 2개의 스위치(SW0, SW1) 입력 처리
- **구현 내용**:
  - GPIO 인터럽트 기반 입력 처리
  - 디바운싱 로직 구현
  - 스위치 상태 변화 감지 및 이벤트 처리
- **필요한 설정**:
  - CONFIG_GPIO=y (이미 활성화됨)
  - GPIO callback 등록
- **예상 코드 변경**:
  - `src/main.c`에 switch interrupt handler 추가
  - 각 스위치에 대한 callback 함수 구현

#### Priority 2: LED 제어 확장 (LED0, LED1, TEST_LED 모두 활용)
**목표**: 3개의 LED를 활용한 다양한 패턴 구현

**구현 내용**:
- LED0(PE2), LED1(PE3), TEST_LED(PE6) 모두 사용
- 스위치 입력에 따라 다른 LED 패턴 표시
- 다양한 LED 패턴 구현 (순차 점멸, 패턴 점멸 등)

### 3단계: 주변장치 통합

#### 3.1 I2C EEPROM 연동 (우선순위: 높음)
**목표**: M24C64 EEPROM (8KB) 읽기/쓰기 기능 구현

**필수 작업**:
- `prj.conf`에 `CONFIG_I2C=y` 활성화
- EEPROM 읽기/쓰기 함수 구현
- 데이터 영속성 테스트 (전원 재시작 후 읽기)

**구현 기능**:
- EEPROM 초기화 및 스캔
- 페이지 쓰기 (32바이트 페이지)
- 데이터 읽기/쓰기
- 데이터 무결성 검증

**예상 작업량**: 중간 복잡도

---

**Priority 2: 고급 입출력 기능**

### 기능 4: 멀티 LED 패턴 제어
- LED0 (PE2), LED1 (PE3), TEST_LED (PE6) 활용
- 여러 LED 패턴 구현 (순차 점등, 깜빡임, 호흡 효과 등)
- 스위치로 패턴 전환
- 예상 구현 시간: 간단함

### 기능 5: 추가 UART 통신
- **하드웨어**: UART4 (PD0/PD1), UART5 (PB5/PB6)
- **기능**:
  - 다중 UART 통신 테스트
  - UART 간 데이터 전송
  - 디버깅 및 로깅용 보조 콘솔
- **Kconfig**: 이미 활성화됨

### 기능 6: RTC 및 시간 관리
- **하드웨어**: RTC with LSE (32.768kHz)
- **기능**:
  - 실시간 시계 구현
  - 타임스탬프 기록
  - 알람 기능
  - Backup SRAM 활용
- **필요 설정**: CONFIG_COUNTER=y

### 기능 7: USB 통신
- **하드웨어**: USB OTG HS (PA11/PA12)
- **기능**:
  - USB CDC ACM (가상 시리얼 포트)
  - USB 콘솔
  - 데이터 전송
- **필요 설정**: CONFIG_USB_DEVICE=y, CONFIG_USB_DEVICE_STACK=y

### 기능 8: RNG (난수 생성)
- **하드웨어**: 하드웨어 RNG
- **기능**:
  - 암호학적으로 안전한 난수 생성
  - 보안 응용
- **필요 설정**: CONFIG_ENTROPY_GENERATOR=y

---

## 권장 구현 순서

### Phase 1: 기본 I/O 완성 (우선순위: 높음)
1. **스위치 입력 처리** (기능 2)
   - GPIO 인터럽트 설정
   - 디바운싱 구현
   - 이벤트 핸들러 작성

2. **멀티 LED 제어** (기능 4)
   - 3개 LED 활용
   - 패턴 구현
   - 스위치로 제어

3. **커스텀 Shell 명령** (기능 3)
   - LED 제어 명령
   - 시스템 상태 표시
   - 설정 변경

### Phase 2: 주변장치 통합 (우선순위: 중간)
4. **I2C EEPROM** (기능 1)
   - I2C 드라이버 활성화
   - EEPROM 읽기/쓰기
   - 데이터 영속성 테스트
   - Shell 명령 추가

5. **추가 UART** (기능 5)
   - UART4, UART5 활성화
   - 통신 테스트
   - 멀티 콘솔 지원

### Phase 3: 고급 기능 (우선순위: 낮음)
6. **RTC** (기능 6)
   - RTC 드라이버 설정
   - 시간 설정/읽기
   - 알람 기능
   - Backup SRAM 활용

7. **USB 통신** (기능 7)
   - USB 스택 활성화
   - CDC ACM 구현
   - USB 콘솔

8. **RNG** (기능 8)
   - Entropy 드라이버
   - 난수 생성 API
   - 테스트

---

## 각 기능별 상세 구현 계획

### 기능 1: I2C EEPROM (M24C64)

**설정 변경**:
```kconfig
# prj.conf에 추가
CONFIG_I2C=y
```

**구현 단계**:
1. I2C 버스 초기화
2. EEPROM 디바이스 바인딩
3. 읽기/쓰기 함수 구현
4. 페이지 쓰기 처리 (32바이트 페이지)
5. Shell 명령 추가:
   - `eeprom read <addr> <len>`
   - `eeprom write <addr> <data>`
   - `eeprom erase`

**예상 코드 크기**: +3 KB Flash, +1 KB RAM

### 기능 2: 스위치 입력 처리

**설정 변경**:
```kconfig
# prj.conf에 추가
CONFIG_GPIO=y  # 이미 활성화됨
```

**구현 단계**:
1. GPIO 인터럽트 콜백 등록
2. 디바운싱 로직 (타이머 기반)
3. 이벤트 핸들러
4. LED 상태 변경 연동

**예상 코드 크기**: +2 KB Flash, +0.5 KB RAM

### 기능 3: Shell 명령어

**설정**: 이미 CONFIG_SHELL=y 활성화됨

**구현할 명령어**:
```
led <0|1|test> <on|off|toggle>
led pattern <0-7>
eeprom read <addr> <len>
eeprom write <addr> <data>
status
```

**예상 코드 크기**: +4 KB Flash, +1 KB RAM

### 기능 6: RTC

**설정 변경**:
```kconfig
CONFIG_COUNTER=y
```

**구현 단계**:
1. RTC 드라이버 초기화
2. 시간 설정/읽기 함수
3. 알람 설정
4. Backup SRAM 데이터 보존
5. Shell 명령:
   - `rtc set <timestamp>`
   - `rtc get`
   - `rtc alarm <seconds>`

**예상 코드 크기**: +3 KB Flash, +0.5 KB RAM

### 기능 7: USB CDC ACM

**설정 변경**:
```kconfig
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_DEVICE=y
CONFIG_USB_CDC_ACM=y
CONFIG_UART_LINE_CTRL=y
```

**구현 단계**:
1. USB 디바이스 초기화
2. CDC ACM 설정
3. USB 콘솔 또는 데이터 전송
4. USB 연결 감지

**예상 코드 크기**: +15 KB Flash, +5 KB RAM

---

## 메모리 예산

### 현재 사용량
- **Flash**: 57 KB / 1024 KB (5.46%)
- **RAM**: 9.4 KB / 320 KB (2.87%)

### 예상 사용량 (모든 기능 구현 후)
- **Flash**: ~200 KB / 1024 KB (~20%)
  - I2C: +3 KB
  - GPIO 인터럽트: +2 KB
  - Shell 명령: +4 KB
  - I2C EEPROM: +3 KB
  - UART 추가: +5 KB
  - RTC: +3 KB
  - USB: +15 KB
  - 기타: +108 KB (코드, 라이브러리)

- **RAM**: ~40 KB / 320 KB (~12.5%)
  - I2C 버퍼: +1 KB
  - GPIO: +0.5 KB
  - Shell: +1 KB
  - USB 버퍼: +5 KB
  - 멀티스레딩: +10 KB
  - 기타: +13 KB

**결론**: 모든 기능 구현 후에도 충분한 여유 공간 확보

---

## 테스트 계획

### 하드웨어 테스트 체크리스트
- [ ] 보드에 펌웨어 플래시
- [ ] 콘솔 출력 확인 (USART1, 115200 baud)
- [ ] LED 깜빡임 확인 (500ms 주기)
- [ ] 시스템 클록 안정성 확인 (275 MHz)
- [ ] 스위치 입력 테스트
- [ ] I2C EEPROM 읽기/쓰기
- [ ] USB 연결 테스트
- [ ] RTC 시간 유지 확인

### 소프트웨어 테스트
- [ ] 단위 테스트 (각 기능별)
- [ ] 통합 테스트 (여러 기능 동시 사용)
- [ ] 부하 테스트 (CPU, 메모리)
- [ ] 장시간 안정성 테스트

---

## 다음 단계

1. **즉시 실행**: 하드웨어 테스트
   ```bash
   cd /home/lyg/work/zephyr_ws/zephyrproject
   source .venv/bin/activate
   west flash
   ```

2. **우선 구현**: Phase 1 기능들
   - 스위치 입력 처리
   - 멀티 LED 제어
   - Shell 명령어

3. **문서화**:
   - 각 기능 구현 후 SESSION_NOTES.md 업데이트
   - API 문서 작성
   - 사용자 가이드 작성

---

**이 계획서는 프로젝트의 전체 로드맵을 제공합니다. 각 단계를 순차적으로 진행하거나, 특정 기능을 우선 구현할 수 있습니다.**

구현을 시작하시려면 어떤 기능부터 시작하고 싶으신지 알려주세요!