# 코드 리뷰 수정 계획서

> **작성일**: 2026-01-28
> **대상**: PARP-01 RFID Reader Session 4 구현
> **우선순위**: Critical → High → Medium → Low

---

## 목차

1. [개요](#1-개요)
2. [Phase 1: 동시성 안전성 수정 (Critical)](#phase-1-동시성-안전성-수정-critical)
3. [Phase 2: 버퍼 관리 개선 (High)](#phase-2-버퍼-관리-개선-high)
4. [Phase 3: 에러 처리 강화 (Medium)](#phase-3-에러-처리-강화-medium)
5. [Phase 4: 코드 품질 개선 (Low)](#phase-4-코드-품질-개선-low)
6. [테스트 계획](#테스트-계획)
7. [예상 작업량](#예상-작업량)

---

## 1. 개요

### 1.1 발견된 문제점 요약

| 우선순위 | 문제 영역 | 개수 | 위험도 |
|----------|-----------|------|--------|
| Critical | 동시성/스레드 안전성 | 3 | 데이터 손상, 시스템 불안정 |
| High | 버퍼 오버플로우/순서 | 3 | 데이터 손실, 통신 오류 |
| Medium | 에러 처리 | 4 | 디버깅 어려움, 복구 불가 |
| Low | 코드 품질 | 4 | 유지보수성 저하 |

### 1.2 수정 원칙

1. **최소 변경**: 기존 동작을 변경하지 않으면서 문제만 수정
2. **단계적 적용**: Phase별로 빌드/테스트 후 다음 단계 진행
3. **역호환성**: 기존 Shell 명령어, API 시그니처 유지

---

## Phase 1: 동시성 안전성 수정 (Critical)

> **목표**: 인터럽트 컨텍스트와 메인 루프 간 데이터 경합 제거
> **예상 LOC**: ~50줄 변경/추가

### Task 1.1: Ring Buffer 리셋 보호

**파일**: `src/uart_router.c`

**문제**: `ring_buf_reset()` 호출 시 ISR과 경합

**수정 내용**:
```c
// 새로운 헬퍼 함수 추가
static void safe_ring_buf_reset_all(uart_router_t *router)
{
    /* ISR 비활성화 */
    uart_irq_rx_disable(router->uart1);
    uart_irq_tx_disable(router->uart1);
    uart_irq_rx_disable(router->uart4);
    uart_irq_tx_disable(router->uart4);

    /* 버퍼 리셋 */
    ring_buf_reset(&router->uart1_rx_ring);
    ring_buf_reset(&router->uart1_tx_ring);
    ring_buf_reset(&router->uart4_rx_ring);
    ring_buf_reset(&router->uart4_tx_ring);

    /* ISR 재활성화 */
    uart_irq_rx_enable(router->uart1);
    uart_irq_rx_enable(router->uart4);
    /* TX는 데이터 있을 때만 활성화하므로 여기서 안 켬 */
}
```

**적용 위치**:
- `uart_router_set_mode()` 내부 (line ~350)
- `uart_router_start_inventory()` 내부 (line ~700)
- `uart_router_stop_inventory()` 내부 (line ~720)

---

### Task 1.2: Inventory 모드 반복 리셋 제거

**파일**: `src/uart_router.c`

**문제**: `process_inventory_mode()`에서 매번 UART1 RX 버퍼 리셋

**현재 코드** (line ~472):
```c
static void process_inventory_mode(uart_router_t *router)
{
    /* CDC ACM input is BLOCKED in inventory mode */
    ring_buf_reset(&router->uart1_rx_ring);  // ← 매 10ms마다!
```

**수정 코드**:
```c
static void process_inventory_mode(uart_router_t *router)
{
    uint8_t buf[128];
    int len;

    /* CDC ACM 입력은 무시 (데이터만 폐기, 리셋하지 않음) */
    len = ring_buf_get(&router->uart1_rx_ring, buf, sizeof(buf));
    if (len > 0) {
        /* 데이터 폐기 (로깅 없음 - 정상 동작) */
    }
```

**이유**: 리셋 대신 데이터만 읽어서 버리면 ISR과 충돌 없음

---

### Task 1.3: HID 타이핑 속도 Atomic 보호

**파일**: `src/usb_hid.c`

**문제**: `typing_speed_cpm` 변수가 여러 스레드에서 비보호 접근

**수정 내용**:
```c
// 파일 상단에 추가
#include <zephyr/sys/atomic.h>

// 변수 선언 변경
static atomic_t typing_speed_cpm = ATOMIC_INIT(HID_TYPING_SPEED_DEFAULT);

// set 함수 수정
int usb_hid_set_typing_speed(uint16_t cpm)
{
    /* Round to nearest step */
    cpm = ((cpm + HID_TYPING_SPEED_STEP / 2) / HID_TYPING_SPEED_STEP)
           * HID_TYPING_SPEED_STEP;

    /* Clamp to valid range */
    if (cpm < HID_TYPING_SPEED_MIN) {
        cpm = HID_TYPING_SPEED_MIN;
    } else if (cpm > HID_TYPING_SPEED_MAX) {
        cpm = HID_TYPING_SPEED_MAX;
    }

    atomic_set(&typing_speed_cpm, cpm);
    LOG_INF("Typing speed set to %u CPM", cpm);
    return 0;
}

// get 함수 수정
uint16_t usb_hid_get_typing_speed(void)
{
    return (uint16_t)atomic_get(&typing_speed_cpm);
}

// send_epc 함수 내부
uint32_t key_delay = cpm_to_delay_ms((uint16_t)atomic_get(&typing_speed_cpm));
```

---

## Phase 2: 버퍼 관리 개선 (High)

> **목표**: 버퍼 오버플로우 시 안전한 복구, 데이터 순서 보장
> **예상 LOC**: ~40줄 변경/추가

### Task 2.1: TX 부분 전송 로직 수정

**파일**: `src/uart_router.c`

**문제**: TX 콜백에서 부분 전송 후 재삽입 시 순서 뒤바뀜

**현재 코드** (line ~174):
```c
if (sent < len) {
    /* Put back unsent data */
    ring_buf_put(&router->uart1_tx_ring, &buf[sent], len - sent);
}
```

**수정 코드**:
```c
if (sent < len) {
    /* 미전송 데이터는 다음 TX ready 때 자동 처리됨 */
    /* ring_buf_get이 peek가 아니므로 데이터 손실 */
    /* 해결책: TX disable 후 완전 전송까지 대기 */
    router->stats.tx_errors++;
    LOG_WRN("UART1 TX incomplete: %d/%d bytes", sent, len);
}
```

**대안 (더 안전한 방식)**: ring_buf 대신 peek + consume 패턴
```c
// TX 콜백 전체 재작성
if (uart_irq_tx_ready(dev)) {
    uint8_t *data;
    uint32_t len = ring_buf_get_claim(&router->uart1_tx_ring, &data, 64);

    if (len > 0) {
        int sent = uart_fifo_fill(dev, data, len);
        ring_buf_get_finish(&router->uart1_tx_ring, sent);
        router->stats.uart1_tx_bytes += sent;
    } else {
        uart_irq_tx_disable(dev);
    }
}
```

---

### Task 2.2: RX 오버런 시 프레임 어셈블러 리셋

**파일**: `src/uart_router.c`

**문제**: RX 버퍼 오버런 시 E310 프레임 동기화 깨짐

**수정 위치**: `uart4_callback()` (line ~196)

**수정 코드**:
```c
if (put < len) {
    router->stats.rx_overruns++;
    LOG_WRN("UART4 RX buffer overrun: lost %d bytes", len - put);

    /* 프레임 동기화 복구를 위해 어셈블러 리셋 */
    frame_assembler_reset(&router->e310_frame);
}
```

---

### Task 2.3: HID Report 버퍼 로컬 할당

**파일**: `src/usb_hid.c`

**문제**: 전역 `hid_report` 버퍼 재진입 시 충돌

**현재 코드** (line ~244):
```c
UDC_STATIC_BUF_DEFINE(hid_report, HID_KBD_REPORT_SIZE);
// ...
memset(hid_report, 0, HID_KBD_REPORT_SIZE);
```

**수정 코드**:
```c
// 전역 버퍼 제거하고 함수 내 로컬 버퍼 사용
int usb_hid_send_epc(const uint8_t *epc, size_t len)
{
    uint8_t report[HID_KBD_REPORT_SIZE];  // 스택 할당

    // ... 기존 코드에서 hid_report → report 로 변경

    memset(report, 0, HID_KBD_REPORT_SIZE);
    report[2] = keycode;
    int ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE, report);
```

**주의**: `UDC_STATIC_BUF_DEFINE`은 DMA 정렬을 위한 것일 수 있음 → Zephyr USB HID 문서 확인 필요

**대안 (DMA 필요 시)**: Mutex로 보호
```c
static K_MUTEX_DEFINE(hid_send_lock);

int usb_hid_send_epc(const uint8_t *epc, size_t len)
{
    k_mutex_lock(&hid_send_lock, K_FOREVER);
    // ... 기존 코드
    k_mutex_unlock(&hid_send_lock);
    return ret;
}
```

---

## Phase 3: 에러 처리 강화 (Medium)

> **목표**: 디버깅 용이성 향상, 장애 복구 능력 강화
> **예상 LOC**: ~30줄 추가

### Task 3.1: CRC 오류 프레임 덤프

**파일**: `src/uart_router.c`

**수정 위치**: `process_inventory_mode()` (line ~500)

**수정 코드**:
```c
int ret = e310_verify_crc(frame, frame_len);
if (ret == E310_OK) {
    process_e310_frame(router, frame, frame_len);
} else {
    LOG_WRN("Frame CRC error (len=%zu)", frame_len);
    LOG_HEXDUMP_DBG(frame, frame_len, "Bad frame");
    router->stats.parse_errors++;
}
```

---

### Task 3.2: Inventory 시작 시 UART4 버퍼 정리

**파일**: `src/uart_router.c`

**수정 위치**: `uart_router_start_inventory()` (line ~700)

**수정 코드**:
```c
int uart_router_start_inventory(uart_router_t *router)
{
    if (!router->uart4_ready) {
        LOG_ERR("UART4 not ready");
        return -ENODEV;
    }

    /* 이전 모드에서 남은 데이터 정리 */
    uart_irq_rx_disable(router->uart4);
    ring_buf_reset(&router->uart4_rx_ring);
    frame_assembler_reset(&router->e310_frame);
    uart_irq_rx_enable(router->uart4);

    /* Build Start Fast Inventory command */
    int len = e310_build_start_fast_inventory(&router->e310_ctx, E310_TARGET_A);
    // ... 나머지 코드
```

---

### Task 3.3: 파라미터 검증 순서 통일

**파일**: `src/usb_hid.c`

**수정 위치**: `usb_hid_send_epc()` (line ~210)

**수정 코드**:
```c
int usb_hid_send_epc(const uint8_t *epc, size_t len)
{
    /* 입력 파라미터 먼저 검증 */
    if (!epc || len == 0) {
        return -EINVAL;
    }

    /* 디바이스 상태 검증 */
    if (!hid_dev) {
        LOG_ERR("HID device not initialized");
        return -ENODEV;
    }

    if (!hid_ready) {
        LOG_WRN("HID interface not ready");
        return -EAGAIN;
    }
    // ... 나머지 코드
```

---

### Task 3.4: e310_format_epc_string 반환값 검증

**파일**: `src/uart_router.c`

**수정 위치**: `process_e310_frame()` (line ~430)

**수정 코드**:
```c
/* Format EPC as hex string */
char epc_str[128];
int fmt_ret = e310_format_epc_string(tag.epc, tag.epc_len,
                                      epc_str, sizeof(epc_str));
if (fmt_ret < 0) {
    LOG_ERR("Failed to format EPC string: %d", fmt_ret);
    router->stats.parse_errors++;
    return;
}

/* Send to USB HID Keyboard */
ret = usb_hid_send_epc((uint8_t *)epc_str, fmt_ret);
```

---

## Phase 4: 코드 품질 개선 (Low)

> **목표**: 유지보수성, 가독성 향상
> **예상 LOC**: ~20줄 변경

### Task 4.1: 매직 넘버 상수화

**파일**: `src/usb_hid.c`

**수정 내용**:
```c
/* CPM (Characters Per Minute) to delay conversion
 * Each character requires 2 HID events (press + release)
 * Delay per event = 60000ms / CPM / 2 = 30000 / CPM
 */
#define HID_EVENTS_PER_CHAR     2
#define MS_PER_MINUTE           60000
#define CPM_TO_DELAY_FACTOR     (MS_PER_MINUTE / HID_EVENTS_PER_CHAR)

static inline uint32_t cpm_to_delay_ms(uint16_t cpm)
{
    if (cpm == 0) {
        return 50; /* Safe default: ~600 CPM */
    }
    return CPM_TO_DELAY_FACTOR / cpm;
}
```

---

### Task 4.2: 중복 toupper() 제거

**파일**: `src/usb_hid.c`

**수정 위치**: `usb_hid_send_epc()` (line ~230)

**수정 코드**:
```c
/* Send each character as HID keycode */
for (size_t i = 0; i < len; i++) {
    char c = (char)epc[i];

    /* ascii_to_hid_keycode() 내부에서 toupper 처리함 */
    uint8_t keycode = ascii_to_hid_keycode(c);
    if (keycode == 0) {
        LOG_DBG("Skipping invalid character: 0x%02X", (uint8_t)c);
        continue;
    }
    // ...
```

**참고**: `ascii_to_hid_keycode()` 함수 첫 줄에 이미 `toupper()` 있음

---

### Task 4.3: 프레임 어셈블러 타임아웃 로직 개선

**파일**: `src/uart_router.c`

**수정 위치**: `frame_assembler_feed()` (line ~56)

**수정 코드**:
```c
/* 모든 상태에서 타임아웃 체크 (WAIT_LEN 포함) */
if (fa->last_byte_time > 0 &&
    (now - fa->last_byte_time) > FRAME_ASSEMBLER_TIMEOUT_MS) {
    LOG_DBG("Frame assembler timeout (state=%d, received=%zu)",
            fa->state, fa->received);
    frame_assembler_reset(fa);
}
```

---

### Task 4.4: 로깅 레벨 통일

**파일**: `src/uart_router.c`, `src/usb_hid.c`

**수정 내용**:
```c
// uart_router.c
LOG_MODULE_REGISTER(uart_router, LOG_LEVEL_INF);  // DBG → INF

// 또는 prj.conf에서 통합 관리
// CONFIG_UART_ROUTER_LOG_LEVEL_INF=y
// CONFIG_USB_HID_LOG_LEVEL_INF=y
```

---

## 테스트 계획

### Phase 1 완료 후 테스트

| 테스트 | 방법 | 예상 결과 |
|--------|------|-----------|
| 동시성 스트레스 | E310 연속 읽기 중 `router mode` 변경 | 시스템 안정, 데이터 무결성 |
| HID 속도 변경 | 태그 읽기 중 `hid speed` 변경 | 즉시 반영, 깨진 문자 없음 |

### Phase 2 완료 후 테스트

| 테스트 | 방법 | 예상 결과 |
|--------|------|-----------|
| TX 오버플로우 | 대량 데이터 CDC로 전송 | 에러 로그, 부분 손실 허용 |
| RX 오버플로우 | 버퍼 크기 축소 후 테스트 | 어셈블러 리셋, 재동기화 |
| HID 재진입 | `hid test` 중 자동 태그 읽기 | 양쪽 모두 정상 전송 |

### Phase 3 완료 후 테스트

| 테스트 | 방법 | 예상 결과 |
|--------|------|-----------|
| CRC 오류 주입 | E310 응답 바이트 변조 | 로그에 hex dump 출력 |
| 모드 전환 | BYPASS → INVENTORY 반복 | 깨끗한 버퍼 상태 |

### 전체 완료 후 회귀 테스트

1. **기능 테스트**: 모든 Shell 명령어 동작 확인
2. **장시간 테스트**: 1시간 연속 인벤토리 (안정성)
3. **메모리 테스트**: RAM 사용량 변화 모니터링

---

## 예상 작업량

| Phase | 예상 LOC | 예상 시간 | 위험도 |
|-------|----------|-----------|--------|
| Phase 1 | ~50 | 30분 | 중 (동작 변경) |
| Phase 2 | ~40 | 25분 | 중 (TX 로직 변경) |
| Phase 3 | ~30 | 15분 | 낮음 |
| Phase 4 | ~20 | 10분 | 낮음 |
| 테스트 | - | 30분 | - |
| **합계** | **~140** | **~2시간** | - |

---

## 파일별 수정 요약

| 파일 | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|------|---------|---------|---------|---------|
| `uart_router.c` | Task 1.1, 1.2 | Task 2.1, 2.2 | Task 3.1, 3.2, 3.4 | Task 4.3 |
| `usb_hid.c` | Task 1.3 | Task 2.3 | Task 3.3 | Task 4.1, 4.2, 4.4 |
| `uart_router.h` | - | - | - | - |
| `usb_hid.h` | - | - | - | - |

---

## 승인

- [x] 계획서 검토 완료 (2026-01-28)
- [x] Phase 1 시작 승인 (2026-01-28)
- [x] Phase 2 시작 승인 (2026-01-28)
- [x] Phase 3 시작 승인 (2026-01-28)
- [x] Phase 4 시작 승인 (2026-01-28)
- [x] 전체 완료 확인 (2026-01-28) - 빌드 성공, 하드웨어 테스트 대기

---

# Part 2: Password Storage 모듈 코드 리뷰 수정

> **작성일**: 2026-01-29
> **대상**: `password_storage.c`, `password_storage.h`, `shell_login.c`, `shell_login.h`

---

## 발견된 문제점 요약

| # | 심각도 | 문제 | 파일 | 상태 |
|---|--------|------|------|------|
| 1 | 🔴 높음 | 타이밍 공격 취약점 | shell_login.c | ✅ 완료 |
| 2 | 🔴 높음 | 마스터 패스워드 하드코딩 | shell_login.h | ✅ 완료 |
| 3 | 🟡 중간 | EEPROM 불필요한 쓰기 | password_storage.c | ✅ 완료 |
| 4 | 🟡 중간 | 코드 중복 | password_storage.c | ✅ 완료 |
| 5 | 🟢 낮음 | 로그에 민감정보 출력 | shell_login.c | ✅ 완료 |

---

## Task P2.1: 타이밍 공격 방어 (🔴 높음)

**문제점**

`strcmp()`는 첫 번째 불일치 바이트에서 즉시 반환하므로 응답 시간 차이로 비밀번호 추측 가능.

**수정 방안**

상수 시간(constant-time) 비교 함수 구현 및 적용.

**변경 파일**: `src/shell_login.c`

**추가 코드**:
```c
/**
 * @brief Constant-time string comparison (timing attack resistant)
 *
 * Compares two strings in constant time to prevent timing attacks.
 * The comparison time depends only on the length of the input string,
 * not on when/where the strings differ.
 */
static bool secure_compare(const char *input, const char *secret)
{
    size_t input_len = strlen(input);
    size_t secret_len = strlen(secret);

    /* Use volatile to prevent compiler optimization */
    volatile uint8_t result = (input_len != secret_len) ? 1 : 0;

    /* Compare all characters of the shorter string */
    size_t cmp_len = (input_len < secret_len) ? input_len : secret_len;
    for (size_t i = 0; i < cmp_len; i++) {
        result |= ((uint8_t)input[i] ^ (uint8_t)secret[i]);
    }

    return (result == 0);
}
```

**적용 위치**:
- `cmd_login()`: line 77 - 마스터 패스워드 비교
- `cmd_login()`: line 98 - 사용자 패스워드 비교
- `cmd_passwd()`: line 174 - 현재 패스워드 검증

---

## Task P2.2: 마스터 패스워드 난독화 (🔴 높음)

**문제점**

마스터 패스워드가 평문으로 헤더 파일에 하드코딩되어 바이너리에서 `strings` 명령으로 추출 가능.

**수정 방안**

XOR 난독화로 바이너리 문자열 검색 방지. (Crypto 라이브러리 의존성 없이 구현)

**변경 파일**: `src/shell_login.h`, `src/shell_login.c`

**shell_login.h 변경**:
```c
/* 기존 (제거) */
// #define SHELL_LOGIN_MASTER_PASSWORD   "pascal1!"

/* 신규: XOR 난독화된 마스터 패스워드 */
/* Original: "pascal1!" XOR 0x5A */
#define SHELL_LOGIN_MASTER_XOR_KEY    0x5A
#define SHELL_LOGIN_MASTER_OBFUSCATED { \
    0x3a, 0x3b, 0x39, 0x39, 0x36, 0x3f, 0x6b, 0x73, 0x00 \
}
```

**shell_login.c 추가**:
```c
/**
 * @brief Verify master password (de-obfuscate and compare)
 */
static bool verify_master_password(const char *input)
{
    static const uint8_t obfuscated[] = SHELL_LOGIN_MASTER_OBFUSCATED;
    char master[sizeof(obfuscated)];

    /* De-obfuscate */
    for (size_t i = 0; i < sizeof(obfuscated) - 1; i++) {
        master[i] = obfuscated[i] ^ SHELL_LOGIN_MASTER_XOR_KEY;
    }
    master[sizeof(obfuscated) - 1] = '\0';

    /* Constant-time comparison */
    return secure_compare(input, master);
}
```

**적용 위치**:
- `cmd_login()`: 마스터 패스워드 검증 로직 교체

---

## Task P2.3: EEPROM 쓰기 최적화 (🟡 중간)

**문제점**

`password_storage_set_master_used()`가 매번 EEPROM에 48바이트 전체 쓰기 수행.
M24C64 EEPROM 쓰기 수명: ~100만 회.

**수정 방안**

플래그가 이미 설정되어 있으면 쓰기 건너뛰기.

**변경 파일**: `src/password_storage.c`

**변경 위치**: `password_storage_set_master_used()` 함수 시작 부분

**변경 코드**:
```c
void password_storage_set_master_used(void)
{
    /* Already set in RAM - skip EEPROM write to reduce wear */
    if (current_flags & FLAG_MASTER_USED) {
        LOG_DBG("Master flag already set, skipping EEPROM write");
        return;
    }

    /* Set flag in RAM */
    current_flags |= FLAG_MASTER_USED;

    /* ... 나머지 EEPROM 쓰기 코드 ... */
}
```

---

## Task P2.4: 코드 중복 제거 (🟡 중간)

**문제점**

기본 패스워드 설정 코드가 `password_storage_init()`에서 5번, `password_storage_reset()`에서 1번 반복됨.

**수정 방안**

헬퍼 함수로 추출.

**변경 파일**: `src/password_storage.c`

**추가 함수**:
```c
/**
 * @brief Load default password into RAM
 */
static void load_default_password(void)
{
    strncpy(current_password, SHELL_LOGIN_DEFAULT_PASSWORD,
            sizeof(current_password) - 1);
    current_password[sizeof(current_password) - 1] = '\0';
}
```

**적용 위치**:
- `password_storage_init()`: 5곳 교체
- `password_storage_reset()`: 1곳 교체

---

## Task P2.5: 로그 민감정보 제거 (🟢 낮음)

**문제점**

`resetpasswd` 명령 실행 시 기본 패스워드가 콘솔에 평문 출력됨.

**수정 방안**

패스워드 대신 성공 메시지만 출력.

**변경 파일**: `src/shell_login.c`

**변경 위치**: `cmd_resetpasswd()` 함수 (line 235)

**기존 코드**:
```c
shell_print(sh, "Password reset to default: %s",
            SHELL_LOGIN_DEFAULT_PASSWORD);
```

**변경 코드**:
```c
shell_print(sh, "Password reset to default.");
shell_print(sh, "Refer to device documentation for credentials.");
```

---

## 수정 순서

| 순서 | Task | 파일 | 설명 |
|------|------|------|------|
| 1 | P2.4 | password_storage.c | 헬퍼 함수 추가, 중복 제거 |
| 2 | P2.3 | password_storage.c | EEPROM 쓰기 최적화 |
| 3 | P2.1 | shell_login.c | secure_compare() 함수 추가 |
| 4 | P2.2 | shell_login.h, shell_login.c | 마스터 패스워드 난독화 |
| 5 | P2.5 | shell_login.c | 로그 민감정보 제거 |
| 6 | - | - | 빌드 및 검증 |

---

## 예상 영향

| 항목 | 변경 전 | 변경 후 |
|------|---------|---------|
| Flash 사용량 | 128,568 B | +150~250 B |
| RAM 사용량 | 29,264 B | 변경 없음 |
| 보안 수준 | 중 | 상 |
| 코드 품질 | 중 | 상 |

---

## 테스트 체크리스트

- [ ] 일반 로그인/로그아웃 정상 동작
- [ ] 마스터 패스워드 로그인 정상 동작 (난독화 후)
- [ ] 비밀번호 변경 후 재부팅 → 변경된 비밀번호 유지
- [ ] resetpasswd 명령 (마스터 세션에서만 동작)
- [ ] 3회 실패 후 lockout 동작
- [ ] EEPROM 없이 동작 (graceful fallback)
- [ ] 빌드 성공 확인

---

## 승인

- [x] 계획서 검토 완료 (2026-01-29)
- [x] 수정 시작 승인 (2026-01-29)
- [x] 전체 완료 확인 (2026-01-29) - 빌드 성공

## 실제 결과

| 항목 | 변경 전 | 변경 후 | 차이 |
|------|---------|---------|------|
| Flash 사용량 | 128,568 B | 128,708 B | +140 B |
| RAM 사용량 | 29,264 B | 29,264 B | 변경 없음 |

---

# Part 3: 양산 적합성 수정

> **작성일**: 2026-01-29
> **대상**: `password_storage.c`, `password_storage.h`, `shell_login.c`, `shell_login.h`
> **목표**: 프로토타입 → 양산 제품 수준으로 품질 향상

---

## 발견된 문제점 요약

| # | 심각도 | 문제 | 파일 | 상태 |
|---|--------|------|------|------|
| 1 | 🔴 필수 | 기본 패스워드 평문 노출 | shell_login.h | ✅ 완료 |
| 2 | 🔴 필수 | 주석에 마스터 패스워드 평문 | shell_login.h | ✅ 완료 |
| 3 | 🔴 필수 | EEPROM Write-Verify 없음 | password_storage.c | ✅ 완료 |
| 4 | 🔴 필수 | 쓰기 실패 시 RAM/EEPROM 불일치 | password_storage.c | ✅ 완료 |
| 5 | 🟡 권장 | Lockout 재부팅으로 우회 | password_storage.c | ✅ 완료 |
| 6 | 🟡 권장 | 패스워드 복잡도 검증 미흡 | shell_login.c | ✅ 완료 |
| 7 | 🟢 선택 | 첫 부팅 강제 패스워드 변경 | shell_login.c | ✅ 완료 |

---

## Task P3.1: 기본 패스워드 난독화 (🔴 필수)

**문제점**

기본 패스워드 `parp2026`이 헤더 파일에 평문으로 존재하여 바이너리에서 추출 가능.

**수정 방안**

마스터 패스워드와 동일한 XOR 난독화 적용.

**변경 파일**: `src/shell_login.h`, `src/shell_login.c`, `src/password_storage.c`

**shell_login.h 변경**:
```c
/* 기존 (제거) */
// #define SHELL_LOGIN_DEFAULT_PASSWORD  "parp2026"

/* 신규: XOR 난독화된 기본 패스워드 */
/* "parp2026" XOR 0x5A */
#define SHELL_LOGIN_DEFAULT_XOR_KEY      0x5A
#define SHELL_LOGIN_DEFAULT_OBFUSCATED { \
    0x2a, 0x3b, 0x28, 0x2a, 0x6a, 0x68, 0x6c, 0x60, 0x00 \
}
```

**shell_login.c 추가**:
```c
/**
 * @brief Get de-obfuscated default password
 */
static const char *get_default_password(void)
{
    static char default_pw[16];
    static bool initialized = false;

    if (!initialized) {
        static const uint8_t obfuscated[] = SHELL_LOGIN_DEFAULT_OBFUSCATED;
        for (size_t i = 0; i < sizeof(obfuscated) - 1; i++) {
            default_pw[i] = (char)(obfuscated[i] ^ SHELL_LOGIN_DEFAULT_XOR_KEY);
        }
        default_pw[sizeof(obfuscated) - 1] = '\0';
        initialized = true;
    }
    return default_pw;
}
```

**password_storage.c 변경**:
- `SHELL_LOGIN_DEFAULT_PASSWORD` 사용 부분을 `get_default_password()` 호출로 변경
- 또는 password_storage.h에 API 추가

---

## Task P3.2: 주석에서 평문 패스워드 제거 (🔴 필수)

**문제점**

`shell_login.h:22`에 마스터 패스워드가 주석으로 노출됨.

**수정 방안**

주석에서 평문 패스워드 완전 제거.

**변경 파일**: `src/shell_login.h`

**기존 코드**:
```c
/**
 * Master password obfuscation (XOR with key to prevent binary string search)
 *
 * This is NOT cryptographic security - it only prevents casual discovery
 * via `strings` command on the binary. The original password is "pascal1!".
 */
```

**변경 코드**:
```c
/**
 * Master password obfuscation (XOR with key to prevent binary string search)
 *
 * This is NOT cryptographic security - it only prevents casual discovery
 * via `strings` command on the binary.
 *
 * NOTE: Master password is documented separately in secure storage.
 * DO NOT add the plain text password here.
 */
```

---

## Task P3.3: EEPROM Write-Read-Verify 구현 (🔴 필수)

**문제점**

EEPROM 쓰기 후 검증이 없어 데이터 무결성 확인 불가.

**수정 방안**

Write 후 Read-back하여 내용 일치 검증.

**변경 파일**: `src/password_storage.c`

**추가 함수**:
```c
/**
 * @brief Write to EEPROM with verification
 *
 * Writes data to EEPROM and reads back to verify integrity.
 *
 * @return 0 on success, -EIO on verification failure, other negative on error
 */
static int eeprom_write_verified(const uint8_t *buf, size_t len)
{
    uint8_t verify[STORAGE_SIZE];
    int ret;

    /* Write to EEPROM */
    ret = eeprom_write_storage(buf, len);
    if (ret < 0) {
        return ret;
    }

    /* Small delay for EEPROM internal write cycle */
    k_msleep(5);

    /* Read back for verification */
    ret = eeprom_read_storage(verify, len);
    if (ret < 0) {
        LOG_ERR("EEPROM verify read failed: %d", ret);
        return ret;
    }

    /* Compare */
    if (memcmp(buf, verify, len) != 0) {
        LOG_ERR("EEPROM write verification failed");
        return -EIO;
    }

    return 0;
}
```

**적용 위치**:
- `init_eeprom_defaults()`: line 142
- `password_storage_save()`: line 297
- `password_storage_set_master_used()`: line 370

---

## Task P3.4: 쓰기 실패 시 롤백 구현 (🔴 필수)

**문제점**

`password_storage_save()`에서 RAM 업데이트 후 EEPROM 쓰기 실패 시 불일치 발생.

**수정 방안**

EEPROM 쓰기 성공 시에만 RAM 업데이트.

**변경 파일**: `src/password_storage.c`

**기존 코드**:
```c
int password_storage_save(const char *new_password)
{
    // ...
    /* Update RAM first */
    strncpy(current_password, new_password, ...);

    /* If EEPROM not available, just keep in RAM */
    if (!eeprom_available) {
        return 0;
    }

    /* Write to EEPROM */
    ret = eeprom_write_storage(buf, STORAGE_SIZE);
    if (ret < 0) {
        return ret;  // RAM에는 새 값, EEPROM에는 이전 값!
    }
}
```

**변경 코드**:
```c
int password_storage_save(const char *new_password)
{
    char backup[32];
    // ...

    /* Backup current password for rollback */
    strncpy(backup, current_password, sizeof(backup));

    /* Update RAM */
    strncpy(current_password, new_password, ...);

    /* If EEPROM not available, just keep in RAM */
    if (!eeprom_available) {
        LOG_WRN("EEPROM not available, password stored in RAM only");
        return 0;
    }

    /* Write to EEPROM with verification */
    ret = eeprom_write_verified(buf, STORAGE_SIZE);
    if (ret < 0) {
        /* Rollback RAM to previous value */
        strncpy(current_password, backup, sizeof(current_password));
        LOG_ERR("EEPROM write failed, password unchanged");
        return ret;
    }
}
```

---

## Task P3.5: Lockout 상태 EEPROM 저장 (🟡 권장)

**문제점**

Lockout 카운터가 RAM에만 저장되어 재부팅으로 우회 가능.

**수정 방안**

EEPROM 데이터 구조에 lockout 필드 추가.

**EEPROM 구조 확장**:
```
Offset  Size  Description
------  ----  -----------
0x0000  4     Magic number (0x50415250)
0x0004  1     Version (0x02)  ← 버전 증가
0x0005  1     Flags
0x0006  1     Failed attempts count  ← 신규
0x0007  1     Reserved
0x0008  32    User password
0x0028  2     CRC-16
0x002A  4     Lockout timestamp (uptime when locked)  ← 신규 (선택)
0x002E  2     Reserved
------  ----  -----------
Total:  48 bytes (변경 없음, Reserved 활용)
```

**주의**: 버전 마이그레이션 로직 필요

---

## Task P3.6: 패스워드 복잡도 검증 (🟡 권장)

**문제점**

패스워드 길이만 검증하여 `1234` 같은 약한 패스워드 허용.

**수정 방안**

최소 복잡도 요구사항 추가.

**변경 파일**: `src/shell_login.c`

**추가 함수**:
```c
/**
 * @brief Validate password complexity
 *
 * Requirements:
 * - Length: 4-31 characters
 * - At least one letter (a-z, A-Z)
 * - At least one digit (0-9)
 *
 * @return true if valid, false otherwise
 */
static bool validate_password_complexity(const char *password, size_t len)
{
    bool has_letter = false;
    bool has_digit = false;

    if (len < 4 || len > PASSWORD_MAX_LEN) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = password[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            has_letter = true;
        } else if (c >= '0' && c <= '9') {
            has_digit = true;
        }
    }

    return has_letter && has_digit;
}
```

**적용 위치**: `cmd_passwd()` 함수

---

## Task P3.7: 첫 부팅 강제 패스워드 변경 (🟢 선택)

**문제점**

기본 패스워드가 모든 제품에 동일하여 보안 취약.

**수정 방안**

첫 부팅 시 패스워드 변경 강제.

**EEPROM Flags 확장**:
```c
#define FLAG_MASTER_USED       0x01
#define FLAG_INITIAL_PW_SET    0x02  /* 신규: 초기 패스워드 변경됨 */
```

**변경 파일**: `src/shell_login.c`, `src/password_storage.c`

**로그인 후 체크 추가**:
```c
/* After successful login */
if (!password_storage_initial_password_changed()) {
    shell_warn(sh, "*** SECURITY WARNING ***");
    shell_warn(sh, "Default password in use. Change immediately!");
    shell_print(sh, "Use: passwd <current> <new>");
}
```

---

## 수정 순서

| 순서 | Task | 우선순위 | 설명 |
|------|------|----------|------|
| 1 | P3.2 | 🔴 필수 | 주석에서 평문 패스워드 제거 |
| 2 | P3.3 | 🔴 필수 | eeprom_write_verified() 함수 추가 |
| 3 | P3.4 | 🔴 필수 | 쓰기 실패 롤백 구현 |
| 4 | P3.1 | 🔴 필수 | 기본 패스워드 난독화 |
| 5 | P3.6 | 🟡 권장 | 패스워드 복잡도 검증 |
| 6 | P3.5 | 🟡 권장 | Lockout EEPROM 저장 |
| 7 | P3.7 | 🟢 선택 | 첫 부팅 강제 변경 |
| 8 | - | - | 빌드 및 검증 |

---

## 예상 영향

| 항목 | 변경 전 | 변경 후 |
|------|---------|---------|
| Flash 사용량 | 128,708 B | +300~500 B |
| RAM 사용량 | 29,264 B | +32~64 B (backup buffer) |
| EEPROM 구조 | v1 (48B) | v2 (48B, 호환) |
| 보안 수준 | 개발용 | 양산용 |

---

## 테스트 체크리스트

### 필수 테스트 (P3.1-P3.4)
- [ ] 기본 패스워드로 로그인 가능
- [ ] `strings` 명령으로 바이너리에서 패스워드 검색 불가
- [ ] EEPROM 쓰기 후 재부팅 → 데이터 유지
- [ ] EEPROM 쓰기 중 전원 차단 시뮬레이션 → 이전 값 유지
- [ ] 주석에 평문 패스워드 없음 확인

### 권장 테스트 (P3.5-P3.6)
- [ ] 3회 실패 후 재부팅 → lockout 유지 (P3.5 적용 시)
- [ ] 약한 패스워드 (`1234`) 거부 (P3.6 적용 시)

### 선택 테스트 (P3.7)
- [ ] 첫 부팅 시 경고 메시지 출력
- [ ] 패스워드 변경 후 경고 해제

---

## 승인

- [x] 계획서 검토 완료 (2026-01-29)
- [x] 필수(🔴) 항목 수정 승인 (2026-01-29)
- [x] 권장(🟡) 항목 수정 승인 (2026-01-29)
- [x] 선택(🟢) 항목 수정 승인 (2026-01-29)
- [x] 전체 완료 확인 (2026-01-29) - 빌드 성공

## 실제 결과 (Part 3)

| 항목 | 변경 전 | 변경 후 | 차이 |
|------|---------|---------|------|
| Flash 사용량 | 128,708 B | 129,940 B | +1,232 B |
| RAM 사용량 | 29,264 B | 29,264 B | 변경 없음 |
| 바이너리 패스워드 노출 | 있음 | 없음 | ✅ 개선 |
