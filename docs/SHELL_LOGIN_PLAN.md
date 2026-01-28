# Shell 보안 및 스위치 제어 구현 계획서

> **작성일**: 2026-01-28
> **대상**: PARP-01 RFID Reader
> **범위**: 스위치 제어 + Shell 로그인 보안

---

## 목차

1. [개요](#1-개요)
2. [현재 문제점](#2-현재-문제점)
3. [요구사항](#3-요구사항)
4. [설계](#4-설계)
5. [구현 계획](#5-구현-계획)
6. [테스트 계획](#6-테스트-계획)
7. [예상 작업량](#7-예상-작업량)

---

## 1. 개요

### 1.1 목적

1. **스위치로 인벤토리 제어** - 하드웨어 버튼으로 인벤토리 On/Off
2. **Shell 로그인 보안** - 비밀번호 인증으로 무단 접근 방지

### 1.2 동작 시나리오

```
┌─────────────────────────────────────────────────────────────────┐
│                        부팅                                      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              인벤토리 모드 (자동 시작)                            │
│              - RFID 태그 자동 읽기                               │
│              - HID 키보드로 EPC 출력                             │
│              - Shell 차단됨                                      │
└─────────────────────────────────────────────────────────────────┘
                              │
                         SW0 버튼 누름
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              IDLE 모드 (인벤토리 중지)                           │
│              - Shell 사용 가능                                   │
│              - 로그인 필요 (비밀번호 입력)                        │
└─────────────────────────────────────────────────────────────────┘
                              │
                         로그인 성공
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              설정 모드                                           │
│              - e310 power 30   (RF 출력 변경)                    │
│              - hid speed 1000  (타이핑 속도 변경)                │
│              - passwd old new  (비밀번호 변경)                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                    SW0 버튼 또는 logout
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              인벤토리 모드 (재시작)                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 현재 문제점

### 2.1 인벤토리 중 Shell 차단

| 문제 | 원인 | 영향 |
|------|------|------|
| Shell 명령어 무응답 | `process_inventory_mode()`에서 CDC 입력 폐기 | 설정 변경 불가 |
| `e310 stop` 동작 안함 | 위와 동일 | 인벤토리 중지 불가 |
| 보드 리셋 필요 | 소프트웨어 제어 수단 없음 | 운영 불편 |

### 2.2 보안 부재

| 문제 | 위험 |
|------|------|
| Shell 무인증 접근 | 누구나 설정 변경 가능 |
| RF 출력 무단 변경 | 태그 읽기 범위 변조 |
| 시스템 명령어 노출 | reboot 등 시스템 명령 실행 가능 |

---

## 3. 요구사항

### 3.1 기능 요구사항

| ID | 요구사항 | 우선순위 | Phase |
|----|----------|----------|-------|
| **FR-01** | SW0으로 인벤토리 On/Off 토글 | **필수** | 1 |
| **FR-02** | 스위치 디바운싱 (50ms) | **필수** | 1 |
| **FR-03** | LED로 현재 모드 표시 | 선택 | 1 |
| **FR-04** | 부팅 시 로그인 필수 | **필수** | 2 |
| **FR-05** | 비밀번호 입력 마스킹 (`****`) | **필수** | 2 |
| **FR-06** | 로그인 전 명령어 제한 | **필수** | 2 |
| **FR-07** | `logout` 명령어 | **필수** | 2 |
| **FR-08** | 3회 실패 시 30초 잠금 | 선택 | 3 |
| **FR-09** | 5분 미사용 시 자동 로그아웃 | 선택 | 3 |
| **FR-10** | `passwd` 비밀번호 변경 | 선택 | 3 |

### 3.2 하드웨어 매핑

| 스위치/LED | GPIO | 기능 |
|------------|------|------|
| **SW0** | PD10 | 인벤토리 On/Off 토글 |
| **SW1** | PD11 | (예비 - 추후 확장) |
| **LED0** | PE2 | 인벤토리 상태 표시 (선택) |
| **TEST_LED** | PE6 | 시스템 동작 표시 (기존) |

---

## 4. 설계

### 4.1 모듈 구조

```
src/
├── switch_control.h    # 스위치 제어 API (신규)
├── switch_control.c    # 스위치 인터럽트 핸들러 (신규)
├── shell_login.h       # 로그인 API (신규)
├── shell_login.c       # 로그인 구현 (신규)
├── uart_router.c       # 인벤토리 모드 수정 (기존)
└── main.c              # 초기화 호출 추가 (기존)
```

### 4.2 상태 다이어그램

```
                    ┌──────────────┐
                    │    BOOT      │
                    └──────┬───────┘
                           │ 자동
                           ▼
    ┌──────────────────────────────────────────┐
    │           INVENTORY_RUNNING              │
    │  - 태그 읽기 활성                         │
    │  - Shell 차단                            │
    │  - LED0 ON (선택)                        │
    └──────────────────┬───────────────────────┘
                       │
                  SW0 누름
                       │
                       ▼
    ┌──────────────────────────────────────────┐
    │           INVENTORY_STOPPED              │
    │  - 태그 읽기 중지                         │
    │  - Shell 활성 (로그인 필요)               │
    │  - LED0 OFF (선택)                       │
    └──────────────────┬───────────────────────┘
                       │
                  SW0 누름
                       │
                       ▼
    ┌──────────────────────────────────────────┐
    │           INVENTORY_RUNNING              │
    │  (위로 돌아감)                            │
    └──────────────────────────────────────────┘
```

### 4.3 API 설계

#### switch_control.h

```c
/**
 * @brief 스위치 제어 초기화
 * @return 0 성공, 음수 실패
 */
int switch_control_init(void);

/**
 * @brief 인벤토리 토글 콜백 등록
 * @param callback 토글 시 호출될 함수
 */
typedef void (*inventory_toggle_cb_t)(bool start);
void switch_control_set_callback(inventory_toggle_cb_t callback);
```

#### shell_login.h

```c
/** 기본 비밀번호 */
#define SHELL_LOGIN_DEFAULT_PASSWORD  "parp2026"

/**
 * @brief 로그인 시스템 초기화
 * @return 0 성공, 음수 실패
 */
int shell_login_init(void);

/**
 * @brief 로그인 상태 확인
 * @return true 로그인됨
 */
bool shell_login_is_authenticated(void);
```

### 4.4 인벤토리 모드 수정

**현재** (`uart_router.c`):
```c
static void process_inventory_mode(uart_router_t *router)
{
    /* CDC 입력 폐기 - Shell 차단됨 */
    len = ring_buf_get(&router->uart1_rx_ring, buf, sizeof(buf));
    (void)len;  // 폐기
    ...
}
```

**수정 후**:
```c
static void process_inventory_mode(uart_router_t *router)
{
    /* CDC 입력은 Shell backend가 직접 처리 */
    /* 여기서는 UART4 (E310) 데이터만 처리 */

    /* E310 응답 처리 */
    len = ring_buf_get(&router->uart4_rx_ring, buf, sizeof(buf));
    ...
}
```

**핵심 변경**: CDC 입력 폐기 코드 제거 → Shell이 항상 동작

---

## 5. 구현 계획

### Phase 1: 스위치 인벤토리 제어 (필수)

> **예상 LOC**: ~120줄
> **파일**: `switch_control.h`, `switch_control.c`, `main.c`, `uart_router.c`

#### Task 1.1: 스위치 드라이버 생성

**파일**: `src/switch_control.h`

```c
#ifndef SWITCH_CONTROL_H
#define SWITCH_CONTROL_H

#include <stdbool.h>

/** 디바운스 시간 (ms) */
#define SWITCH_DEBOUNCE_MS  50

/** 인벤토리 토글 콜백 타입 */
typedef void (*inventory_toggle_cb_t)(bool start);

/**
 * @brief 스위치 제어 초기화
 * @return 0 성공, 음수 실패
 */
int switch_control_init(void);

/**
 * @brief 인벤토리 토글 콜백 등록
 */
void switch_control_set_inventory_callback(inventory_toggle_cb_t cb);

/**
 * @brief 현재 인벤토리 상태 조회
 */
bool switch_control_is_inventory_running(void);

#endif /* SWITCH_CONTROL_H */
```

#### Task 1.2: 스위치 인터럽트 구현

**파일**: `src/switch_control.c`

```c
#include "switch_control.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(switch_control, LOG_LEVEL_INF);

/* SW0 device tree 참조 */
#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

/* 상태 변수 */
static bool inventory_running = true;  /* 부팅 시 인벤토리 시작 */
static inventory_toggle_cb_t toggle_callback = NULL;
static struct gpio_callback sw0_cb_data;

/* 디바운스용 work queue */
static struct k_work_delayable debounce_work;
static int64_t last_press_time = 0;

static void debounce_handler(struct k_work *work)
{
    /* 인벤토리 토글 */
    inventory_running = !inventory_running;

    LOG_INF("SW0: Inventory %s", inventory_running ? "STARTED" : "STOPPED");

    if (toggle_callback) {
        toggle_callback(inventory_running);
    }
}

static void sw0_pressed(const struct device *dev,
                        struct gpio_callback *cb, uint32_t pins)
{
    int64_t now = k_uptime_get();

    /* 디바운스 체크 */
    if ((now - last_press_time) < SWITCH_DEBOUNCE_MS) {
        return;
    }
    last_press_time = now;

    /* 디바운스 후 처리 */
    k_work_schedule(&debounce_work, K_MSEC(SWITCH_DEBOUNCE_MS));
}

int switch_control_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&sw0)) {
        LOG_ERR("SW0 GPIO not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&sw0, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure SW0: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure SW0 interrupt: %d", ret);
        return ret;
    }

    gpio_init_callback(&sw0_cb_data, sw0_pressed, BIT(sw0.pin));
    gpio_add_callback(sw0.port, &sw0_cb_data);

    k_work_init_delayable(&debounce_work, debounce_handler);

    LOG_INF("Switch control initialized (SW0 = PD10)");
    return 0;
}

void switch_control_set_inventory_callback(inventory_toggle_cb_t cb)
{
    toggle_callback = cb;
}

bool switch_control_is_inventory_running(void)
{
    return inventory_running;
}
```

#### Task 1.3: main.c 연동

**파일**: `src/main.c`

```c
#include "switch_control.h"

/* 인벤토리 토글 콜백 */
static void on_inventory_toggle(bool start)
{
    if (start) {
        uart_router_start_inventory(&uart_router);
    } else {
        uart_router_stop_inventory(&uart_router);
    }
}

int main(void)
{
    ...

    /* 스위치 제어 초기화 */
    ret = switch_control_init();
    if (ret < 0) {
        LOG_WRN("Switch control init failed: %d", ret);
    } else {
        switch_control_set_inventory_callback(on_inventory_toggle);
    }

    ...
}
```

#### Task 1.4: CDC 입력 폐기 제거

**파일**: `src/uart_router.c`

```c
static void process_inventory_mode(uart_router_t *router)
{
    uint8_t buf[128];
    int len;

    /* 삭제: CDC 입력 폐기 코드 제거 */
    /* Shell backend가 CDC 입력을 직접 처리하도록 함 */

    // 삭제된 코드:
    // len = ring_buf_get(&router->uart1_rx_ring, buf, sizeof(buf));
    // (void)len;

    /* E310 응답만 처리 */
    len = ring_buf_get(&router->uart4_rx_ring, buf, sizeof(buf));
    if (len <= 0) {
        return;
    }

    /* ... 나머지 프레임 처리 코드 ... */
}
```

#### Task 1.5: CMakeLists.txt 수정

```cmake
target_sources(app PRIVATE
    src/main.c
    src/usb_device.c
    src/usb_hid.c
    src/uart_router.c
    src/e310_protocol.c
    src/e310_test.c
    src/switch_control.c    # 추가
)
```

---

### Phase 2: Shell 로그인 (필수)

> **예상 LOC**: ~100줄
> **파일**: `shell_login.h`, `shell_login.c`, `prj.conf`

#### Task 2.1: Kconfig 설정

**파일**: `prj.conf`

```
# Shell Login Feature
CONFIG_SHELL_START_OBSCURED=y
CONFIG_SHELL_CMDS_SELECT=y
```

#### Task 2.2: 로그인 모듈 구현

**파일**: `src/shell_login.h`

```c
#ifndef SHELL_LOGIN_H
#define SHELL_LOGIN_H

#include <stdbool.h>

#define SHELL_LOGIN_DEFAULT_PASSWORD  "parp2026"

int shell_login_init(void);
bool shell_login_is_authenticated(void);

#endif
```

**파일**: `src/shell_login.c`

```c
#include "shell_login.h"
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(shell_login, LOG_LEVEL_INF);

static bool authenticated = false;
static char password[32] = SHELL_LOGIN_DEFAULT_PASSWORD;

static int cmd_login(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_print(sh, "Usage: login <password>");
        return -EINVAL;
    }

    if (strcmp(argv[1], password) == 0) {
        authenticated = true;
        shell_obscure_set(sh, false);
        shell_set_root_cmd(NULL);
        shell_print(sh, "Login successful. Type 'help' for commands.");
        LOG_INF("Shell login successful");
    } else {
        shell_error(sh, "Invalid password");
        LOG_WRN("Shell login failed");
    }

    return 0;
}

static int cmd_logout(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    authenticated = false;
    shell_obscure_set(sh, true);
    shell_set_root_cmd("login");
    shell_print(sh, "Logged out.");
    LOG_INF("Shell logout");

    return 0;
}

int shell_login_init(void)
{
    /* 부팅 시에는 main.c에서 shell backend를 얻어서 설정 */
    LOG_INF("Shell login initialized (default password: %s)",
            SHELL_LOGIN_DEFAULT_PASSWORD);
    return 0;
}

bool shell_login_is_authenticated(void)
{
    return authenticated;
}

SHELL_CMD_REGISTER(login, NULL, "Login with password", cmd_login);
SHELL_CMD_REGISTER(logout, NULL, "Logout from shell", cmd_logout);
```

#### Task 2.3: main.c 로그인 초기화

```c
#include "shell_login.h"
#include <zephyr/shell/shell.h>

int main(void)
{
    ...

    /* Shell 로그인 초기화 */
    shell_login_init();

    /* Shell backend 설정 */
    const struct shell *sh = shell_backend_uart_get_ptr();
    if (sh) {
        shell_obscure_set(sh, true);   /* 비밀번호 마스킹 */
        shell_set_root_cmd("login");   /* login만 허용 */
    }

    ...
}
```

---

### Phase 3: 보안 강화 (선택)

> **예상 LOC**: ~80줄
> **파일**: `shell_login.c`

#### Task 3.1: 로그인 실패 제한

```c
#define LOGIN_MAX_ATTEMPTS  3
#define LOGIN_LOCKOUT_SEC   30

static uint8_t failed_attempts = 0;
static int64_t lockout_until = 0;

static int cmd_login(const struct shell *sh, size_t argc, char **argv)
{
    int64_t now = k_uptime_get();

    /* 잠금 상태 확인 */
    if (lockout_until > now) {
        int remaining = (lockout_until - now) / 1000;
        shell_error(sh, "Locked. Try again in %d seconds.", remaining);
        return -EACCES;
    }

    if (argc != 2) {
        shell_print(sh, "Usage: login <password>");
        return -EINVAL;
    }

    if (strcmp(argv[1], password) == 0) {
        authenticated = true;
        failed_attempts = 0;
        shell_obscure_set(sh, false);
        shell_set_root_cmd(NULL);
        shell_print(sh, "Login successful.");
    } else {
        failed_attempts++;
        shell_error(sh, "Invalid password (%d/%d)",
                    failed_attempts, LOGIN_MAX_ATTEMPTS);

        if (failed_attempts >= LOGIN_MAX_ATTEMPTS) {
            lockout_until = now + (LOGIN_LOCKOUT_SEC * 1000);
            shell_error(sh, "Too many attempts. Locked for %d seconds.",
                        LOGIN_LOCKOUT_SEC);
            failed_attempts = 0;
        }
    }

    return 0;
}
```

#### Task 3.2: 자동 로그아웃

```c
#define LOGIN_TIMEOUT_SEC  300  /* 5분 */

static int64_t last_activity = 0;

void shell_login_check_timeout(void)
{
    if (!authenticated) return;

    int64_t now = k_uptime_get();
    if ((now - last_activity) > (LOGIN_TIMEOUT_SEC * 1000)) {
        authenticated = false;
        LOG_INF("Auto logout due to inactivity");
        /* Note: shell 설정은 다음 명령어 입력 시 적용 */
    }
}

/* main loop에서 주기적 호출 */
```

#### Task 3.3: 비밀번호 변경

```c
static int cmd_passwd(const struct shell *sh, size_t argc, char **argv)
{
    if (!authenticated) {
        shell_error(sh, "Login required");
        return -EACCES;
    }

    if (argc != 3) {
        shell_print(sh, "Usage: passwd <old_password> <new_password>");
        return -EINVAL;
    }

    if (strcmp(argv[1], password) != 0) {
        shell_error(sh, "Current password incorrect");
        return -EACCES;
    }

    if (strlen(argv[2]) < 4 || strlen(argv[2]) > 31) {
        shell_error(sh, "Password must be 4-31 characters");
        return -EINVAL;
    }

    strncpy(password, argv[2], sizeof(password) - 1);
    shell_print(sh, "Password changed successfully");
    LOG_INF("Password changed");

    return 0;
}

SHELL_CMD_REGISTER(passwd, NULL, "Change password", cmd_passwd);
```

---

## 6. 테스트 계획

### Phase 1 테스트

| 테스트 | 방법 | 예상 결과 |
|--------|------|-----------|
| SW0 인터럽트 | 버튼 누름 | 로그에 "SW0: Inventory STOPPED" |
| 디바운싱 | 빠르게 여러 번 누름 | 한 번만 토글 |
| 인벤토리 중지 | SW0 누름 | 태그 읽기 중단, Shell 응답 |
| 인벤토리 재시작 | SW0 다시 누름 | 태그 읽기 재개 |
| Shell 동작 | 인벤토리 중 `help` 입력 | 명령어 목록 출력 |

### Phase 2 테스트

| 테스트 | 방법 | 예상 결과 |
|--------|------|-----------|
| 부팅 후 상태 | 터미널 연결 | 입력이 `****`로 표시 |
| 명령어 제한 | `e310 start` 입력 | "command not found" |
| 로그인 실패 | 잘못된 비밀번호 | "Invalid password" |
| 로그인 성공 | `login parp2026` | "Login successful" |
| 로그아웃 | `logout` | 마스킹 복귀, 명령어 제한 |

### Phase 3 테스트

| 테스트 | 방법 | 예상 결과 |
|--------|------|-----------|
| 3회 실패 | 3번 틀린 비밀번호 | "Locked for 30 seconds" |
| 자동 로그아웃 | 5분 방치 | 자동 로그아웃 |
| 비밀번호 변경 | `passwd parp2026 newpw` | "Password changed" |

### 통합 테스트

| 시나리오 | 단계 |
|----------|------|
| 전체 흐름 | 부팅 → 인벤토리 동작 → SW0 → 로그인 → 설정변경 → logout → SW0 → 인벤토리 |

---

## 7. 예상 작업량

| Phase | 설명 | LOC | 예상 시간 |
|-------|------|-----|-----------|
| Phase 1 | 스위치 제어 | ~120 | 40분 |
| Phase 2 | Shell 로그인 | ~100 | 30분 |
| Phase 3 | 보안 강화 | ~80 | 25분 |
| 테스트 | 기능 검증 | - | 30분 |
| **합계** | | **~300** | **~2시간** |

---

## 8. 파일별 수정 요약

| 파일 | Phase 1 | Phase 2 | Phase 3 |
|------|---------|---------|---------|
| `prj.conf` | - | Kconfig 추가 | - |
| `CMakeLists.txt` | 소스 추가 | 소스 추가 | - |
| `switch_control.h` | 신규 생성 | - | - |
| `switch_control.c` | 신규 생성 | - | - |
| `shell_login.h` | - | 신규 생성 | - |
| `shell_login.c` | - | 신규 생성 | 잠금/타임아웃/passwd |
| `uart_router.c` | CDC 폐기 제거 | - | - |
| `main.c` | 스위치 초기화 | 로그인 초기화 | 타임아웃 체크 |

---

## 9. 승인

- [x] 계획서 검토 완료 (2026-01-28)
- [x] Phase 1 시작 승인 (스위치 제어) - 구현 완료
- [x] Phase 2 시작 승인 (Shell 로그인) - 구현 완료
- [x] Phase 3 시작 승인 (보안 강화) - 구현 완료
- [x] 전체 완료 확인 (2026-01-28) - 빌드 성공, 하드웨어 테스트 대기

---

## 10. 참고 자료

- Zephyr GPIO Interrupt: https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html
- Zephyr Shell: https://docs.zephyrproject.org/latest/services/shell/index.html
- `CONFIG_SHELL_START_OBSCURED`: 입력 마스킹
- `CONFIG_SHELL_CMDS_SELECT`: 명령어 제한
