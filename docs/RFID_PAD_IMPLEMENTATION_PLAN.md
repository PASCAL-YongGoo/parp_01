# RFID 패드 동작 구현 계획서

> **작성일**: 2026-02-02
> **수정일**: 2026-02-02 (USB Mute 기능 추가, SW0 모드 전환 추가)
> **목적**: RFID 패드로 동작하기 위한 HID 출력 활성화 및 중복 EPC 필터링 구현

---

## 1. 현재 상태 분석

### 1.1 요구사항

| 항목 | 요구사항 |
|------|----------|
| RFID 읽기 | E310 모듈로 태그 읽기 |
| 외부 출력 | USB HID 키보드로 EPC 값 출력 |
| 타수 제어 | 100타/200타/300타... 단위로 출력 속도 조절 |
| 중복 제거 | N초 이내 동일 EPC 재전송 방지 |
| USB 제어 | Shell에서 HID/CDC 출력 On/Off 제어 |
| SW0 모드 전환 | 버튼으로 Inventory/Debug 모드 전환 |

### 1.2 현재 구현 상태

| 기능 | 상태 | 비고 |
|------|------|------|
| E310 RFID 통신 | ✅ 완료 | UART4, 프로토콜 파싱 |
| EPC 파싱 | ✅ 완료 | auto-upload, inventory 응답 |
| **USB HID 출력** | ❌ **비활성화** | 디버깅용으로 주석 처리됨 |
| **중복 EPC 필터링** | ❌ **미구현** | 구현 필요 |
| 타수 제어 (CPM) | ✅ 완료 | 100~1500 CPM 설정 가능 |
| **USB On/Off 제어** | ❌ **미구현** | 구현 필요 |
| **SW0 모드 전환** | ⚠️ **부분 구현** | 콜백 수정 필요 |

### 1.3 비활성화된 코드 위치

| 파일 | 라인 | 내용 |
|------|------|------|
| `nucleo_h723zg_parp01.dts` | 214-224 | `hid_0` 디바이스 노드 주석 처리 |
| `main.c` | 136-146 | `parp_usb_hid_init()` 호출 주석 처리 |
| `uart_router.c` | 14 | `#include "usb_hid.h"` 주석 처리 |
| `uart_router.c` | 475-486 | `usb_hid_send_epc()` 호출 없음 |

---

## 2. 구현 계획

### Phase 1: USB HID 활성화

#### Task 1.1: DTS HID 디바이스 활성화

**파일**: `boards/arm/nucleo_h723zg_parp01/nucleo_h723zg_parp01.dts`

**변경 내용**:
```dts
/* 현재 (주석 처리됨) */
/* HID keyboard - disabled for bypass debugging
hid_0: hid_0 {
    compatible = "zephyr,hid-device";
    ...
};
*/

/* 변경 후 */
hid_0: hid_0 {
    compatible = "zephyr,hid-device";
    label = "HID_KEYBOARD";
    protocol-code = "keyboard";
    in-report-size = <8>;
    out-report-size = <1>;
    in-polling-period-us = <1000>;
    out-polling-period-us = <1000>;
};
```

#### Task 1.2: main.c HID 초기화 활성화

**파일**: `src/main.c`

**변경 내용**:
```c
/* 현재 (주석 처리됨) */
/* USB HID disabled for UART4 bypass debugging
ret = parp_usb_hid_init();
...
*/
printk("USB HID disabled (bypass mode)\n");

/* 변경 후 */
printk("Init USB HID...\n");
ret = parp_usb_hid_init();
if (ret < 0) {
    printk("USB HID init failed: %d (continuing anyway)\n", ret);
    LOG_ERR("Failed to init USB HID: %d", ret);
} else {
    printk("USB HID OK\n");
}
```

#### Task 1.3: uart_router.c HID 연결

**파일**: `src/uart_router.c`

**변경 내용**:
```c
/* 현재 */
/* #include "usb_hid.h" */  /* Disabled for bypass debugging */

/* 변경 후 */
#include "usb_hid.h"
```

---

### Phase 2: USB Mute 기능 구현 (소프트웨어 레벨 On/Off)

> **설계 방식**: USB 연결은 유지하고, 데이터 전송만 차단 (Mute)
> **기본값**: 개발 단계이므로 **Off (Muted)** 상태로 시작

#### Task 2.1: USB Mute 상태 변수 추가

**파일**: `src/usb_hid.c`

```c
/** HID 출력 Mute 상태 (기본값: true = 출력 차단) */
static bool hid_muted = true;

/**
 * @brief HID 출력 활성화/비활성화
 * @param enable true: 출력 활성화, false: 출력 차단
 */
void usb_hid_set_enabled(bool enable)
{
    hid_muted = !enable;
    LOG_INF("HID output %s", enable ? "enabled" : "disabled (muted)");
}

/**
 * @brief HID 출력 상태 조회
 * @return true: 출력 활성화됨, false: 출력 차단됨
 */
bool usb_hid_is_enabled(void)
{
    return !hid_muted;
}
```

**파일**: `src/usb_hid.h`

```c
/**
 * @brief HID 출력 활성화/비활성화 (소프트웨어 Mute)
 * @param enable true: 출력 활성화, false: 출력 차단
 */
void usb_hid_set_enabled(bool enable);

/**
 * @brief HID 출력 상태 조회
 * @return true: 출력 활성화됨, false: 출력 차단됨
 */
bool usb_hid_is_enabled(void);
```

#### Task 2.2: usb_hid_send_epc() Mute 체크 추가

**파일**: `src/usb_hid.c`

```c
int usb_hid_send_epc(const uint8_t *epc, size_t len)
{
    /* Mute 체크 - 차단 시 성공 반환 (호출자에게 투명) */
    if (hid_muted) {
        LOG_DBG("HID muted, EPC not sent");
        return 0;
    }
    
    /* 기존 코드... */
}
```

#### Task 2.3: CDC Mute 기능 추가

**파일**: `src/uart_router.c`

```c
/** CDC 출력 Mute 상태 (기본값: true = 출력 차단) */
static bool cdc_muted = true;

/**
 * @brief CDC 출력 활성화/비활성화
 */
void uart_router_set_cdc_enabled(bool enable)
{
    cdc_muted = !enable;
    LOG_INF("CDC output %s", enable ? "enabled" : "disabled (muted)");
}

bool uart_router_is_cdc_enabled(void)
{
    return !cdc_muted;
}
```

**파일**: `src/uart_router.h`

```c
void uart_router_set_cdc_enabled(bool enable);
bool uart_router_is_cdc_enabled(void);
```

#### Task 2.4: CDC 전송 함수에 Mute 체크 추가

**파일**: `src/uart_router.c`

```c
int uart_router_send_uart1(uart_router_t *router, const uint8_t *data, size_t len)
{
    /* Mute 체크 */
    if (cdc_muted) {
        return 0;  /* 차단 시 성공 반환 */
    }
    
    /* 기존 코드... */
}
```

---

### Phase 3: EPC 중복 필터링 구현

#### Task 3.1: EPC 캐시 구조체 정의

**파일**: `src/uart_router.c` (또는 새 파일 `src/epc_filter.h/c`)

**설계**:
```c
/** 최대 캐시 EPC 개수 */
#define EPC_CACHE_SIZE          32

/** 기본 debounce 시간 (초) */
#define EPC_DEBOUNCE_DEFAULT_SEC  3

/** EPC 캐시 엔트리 */
typedef struct {
    uint8_t epc[E310_MAX_EPC_LENGTH];  /* EPC 데이터 */
    uint8_t epc_len;                    /* EPC 길이 */
    int64_t timestamp;                  /* 마지막 전송 시간 (ms) */
} epc_cache_entry_t;

/** EPC 필터 컨텍스트 */
typedef struct {
    epc_cache_entry_t entries[EPC_CACHE_SIZE];
    uint8_t count;                      /* 현재 캐시된 EPC 수 */
    uint8_t next_idx;                   /* 다음 삽입 위치 (circular) */
    uint32_t debounce_ms;               /* Debounce 시간 (ms) */
} epc_filter_t;
```

#### Task 3.2: EPC 필터 API 구현

**함수 목록**:
```c
/**
 * @brief EPC 필터 초기화
 * @param filter 필터 컨텍스트
 * @param debounce_sec Debounce 시간 (초)
 */
void epc_filter_init(epc_filter_t *filter, uint32_t debounce_sec);

/**
 * @brief EPC 중복 체크 및 캐시 업데이트
 * @param filter 필터 컨텍스트
 * @param epc EPC 데이터
 * @param epc_len EPC 길이
 * @return true: 전송 허용 (새 EPC 또는 debounce 시간 경과)
 *         false: 전송 차단 (중복, debounce 시간 내)
 */
bool epc_filter_check(epc_filter_t *filter, const uint8_t *epc, uint8_t epc_len);

/**
 * @brief Debounce 시간 설정
 * @param filter 필터 컨텍스트
 * @param debounce_sec Debounce 시간 (초)
 */
void epc_filter_set_debounce(epc_filter_t *filter, uint32_t debounce_sec);

/**
 * @brief 캐시 초기화 (인벤토리 시작 시)
 * @param filter 필터 컨텍스트
 */
void epc_filter_clear(epc_filter_t *filter);
```

#### Task 3.3: EPC 필터 로직 구현

**알고리즘**:
```
epc_filter_check(filter, epc, epc_len):
    1. 캐시에서 동일 EPC 검색
    2. if (동일 EPC 발견):
           if (현재시간 - timestamp < debounce_ms):
               return false  // 차단
           else:
               timestamp = 현재시간
               return true   // 허용 (시간 경과)
    3. else:
           캐시에 새 EPC 추가 (circular buffer)
           return true       // 허용 (새 EPC)
```

#### Task 3.4: uart_router에 EPC 필터 통합

**파일**: `src/uart_router.h`

**변경 내용**:
```c
typedef struct {
    /* 기존 필드들... */
    
    /* EPC 필터 (Phase 2 추가) */
    epc_filter_t epc_filter;
} uart_router_t;
```

**파일**: `src/uart_router.c`

**변경 위치**: `process_e310_frame()` 함수

```c
/* 현재 */
char epc_str[128];
int fmt_ret = e310_format_epc_string(tag.epc, tag.epc_len, epc_str, sizeof(epc_str));
// ... 로그 출력만 ...
beep_control_trigger();

/* 변경 후 */
char epc_str[128];
int fmt_ret = e310_format_epc_string(tag.epc, tag.epc_len, epc_str, sizeof(epc_str));

/* EPC 중복 필터링 */
if (epc_filter_check(&router->epc_filter, tag.epc, tag.epc_len)) {
    /* 새 EPC 또는 debounce 시간 경과 → HID 전송 */
    int hid_ret = usb_hid_send_epc((const uint8_t *)epc_str, strlen(epc_str));
    if (hid_ret >= 0) {
        router->stats.epc_sent++;
        LOG_INF("EPC #%u: %s (RSSI: %u)", router->stats.epc_sent, epc_str, tag.rssi);
        beep_control_trigger();
    } else {
        LOG_WRN("HID send failed: %d", hid_ret);
    }
} else {
    /* 중복 EPC → 무시 */
    LOG_DBG("Duplicate EPC ignored: %s", epc_str);
}
```

---

### Phase 4: Shell 명령어 추가

#### Task 4.1: USB On/Off 명령어

**파일**: `src/uart_router.c` (또는 새 파일)

**명령어**: `usb hid on/off`, `usb cdc on/off`

```c
/* USB 하위 명령어 */
static int cmd_usb_hid(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_print(sh, "HID output: %s", usb_hid_is_enabled() ? "ON" : "OFF");
        shell_print(sh, "Usage: usb hid <on|off>");
        return 0;
    }
    
    if (strcmp(argv[1], "on") == 0) {
        usb_hid_set_enabled(true);
        shell_print(sh, "HID output enabled");
    } else if (strcmp(argv[1], "off") == 0) {
        usb_hid_set_enabled(false);
        shell_print(sh, "HID output disabled (muted)");
    } else {
        shell_error(sh, "Invalid argument: %s (use on/off)", argv[1]);
        return -EINVAL;
    }
    return 0;
}

static int cmd_usb_cdc(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_print(sh, "CDC output: %s", uart_router_is_cdc_enabled() ? "ON" : "OFF");
        shell_print(sh, "Usage: usb cdc <on|off>");
        return 0;
    }
    
    if (strcmp(argv[1], "on") == 0) {
        uart_router_set_cdc_enabled(true);
        shell_print(sh, "CDC output enabled");
    } else if (strcmp(argv[1], "off") == 0) {
        uart_router_set_cdc_enabled(false);
        shell_print(sh, "CDC output disabled (muted)");
    } else {
        shell_error(sh, "Invalid argument: %s (use on/off)", argv[1]);
        return -EINVAL;
    }
    return 0;
}

static int cmd_usb_status(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "=== USB Status ===");
    shell_print(sh, "HID output: %s", usb_hid_is_enabled() ? "ON" : "OFF (muted)");
    shell_print(sh, "CDC output: %s", uart_router_is_cdc_enabled() ? "ON" : "OFF (muted)");
    shell_print(sh, "HID ready: %s", usb_hid_is_ready() ? "yes" : "no");
    return 0;
}

/* Shell 명령어 등록 */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_usb,
    SHELL_CMD(hid, NULL, "HID output on/off", cmd_usb_hid),
    SHELL_CMD(cdc, NULL, "CDC output on/off", cmd_usb_cdc),
    SHELL_CMD(status, NULL, "Show USB status", cmd_usb_status),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(usb, &sub_usb, "USB control commands", NULL);
```

#### Task 4.2: HID debounce 명령어

**파일**: `src/uart_router.c`

**명령어**: `hid debounce [seconds]`

```c
static int cmd_hid_debounce(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_print(sh, "Current debounce: %u seconds",
                    g_router_instance->epc_filter.debounce_ms / 1000);
        shell_print(sh, "Usage: hid debounce <seconds>");
        return 0;
    }
    
    uint32_t sec = atoi(argv[1]);
    epc_filter_set_debounce(&g_router_instance->epc_filter, sec);
    shell_print(sh, "Debounce set to %u seconds", sec);
    return 0;
}
```

#### Task 4.3: EPC 캐시 초기화 명령어

**명령어**: `hid clear`

```c
static int cmd_hid_clear(const struct shell *sh, size_t argc, char **argv)
{
    epc_filter_clear(&g_router_instance->epc_filter);
    shell_print(sh, "EPC cache cleared");
    return 0;
}
```

#### Task 4.4: HID 상태 명령어 업데이트

**명령어**: `hid status`

```c
/* 기존 출력에 추가 */
shell_print(sh, "EPC Filter:");
shell_print(sh, "  Debounce: %u sec", filter->debounce_ms / 1000);
shell_print(sh, "  Cached EPCs: %u/%u", filter->count, EPC_CACHE_SIZE);
```

---

### Phase 5: 인벤토리 시작 시 캐시 초기화

#### Task 5.1: 인벤토리 시작 시 EPC 캐시 클리어

**파일**: `src/uart_router.c`

**위치**: `uart_router_start_inventory()`

```c
int uart_router_start_inventory(uart_router_t *router)
{
    /* 기존 코드... */
    
    /* EPC 캐시 초기화 (새 인벤토리 세션) */
    epc_filter_clear(&router->epc_filter);
    
    /* 기존 코드... */
}
```

---

### Phase 6: SW0 버튼 모드 전환 연동

> **목적**: SW0 버튼으로 Inventory Mode / Debug Mode 자동 전환
> **기본값**: Debug Mode (개발 단계)

#### 동작 모드 정의

| 모드 | Inventory | HID 출력 | CDC 출력 | 용도 |
|------|-----------|----------|----------|------|
| **Inventory Mode** | ON | **ON** | **OFF** | RFID 패드 운영 |
| **Debug Mode** | OFF | **OFF** | **ON** | 개발/디버깅 |

#### Task 6.1: main.c 인벤토리 토글 콜백 수정

**파일**: `src/main.c`

**현재 코드**:
```c
static void on_inventory_toggle(bool start)
{
    if (start) {
        shell_login_force_logout();
        uart_router_start_inventory(&uart_router);
    } else {
        uart_router_stop_inventory(&uart_router);
    }
}
```

**변경 후**:
```c
static void on_inventory_toggle(bool start)
{
    if (start) {
        /* ===== Inventory Mode: RFID 패드 운영 ===== */
        LOG_INF("Switching to Inventory Mode");
        
        /* 보안: 로그아웃 */
        shell_login_force_logout();
        
        /* USB 출력 전환: HID ON, CDC OFF */
        usb_hid_set_enabled(true);
        uart_router_set_cdc_enabled(false);
        
        /* E310 인벤토리 시작 */
        uart_router_start_inventory(&uart_router);
        
        LOG_INF("Inventory Mode: HID=ON, CDC=OFF");
    } else {
        /* ===== Debug Mode: 개발/디버깅 ===== */
        LOG_INF("Switching to Debug Mode");
        
        /* E310 인벤토리 중지 */
        uart_router_stop_inventory(&uart_router);
        
        /* USB 출력 전환: HID OFF, CDC ON */
        usb_hid_set_enabled(false);
        uart_router_set_cdc_enabled(true);
        
        LOG_INF("Debug Mode: HID=OFF, CDC=ON");
    }
}
```

#### Task 6.2: 부팅 시 기본 모드 설정

**파일**: `src/main.c`

**위치**: `main()` 함수 초기화 부분

```c
/* 기본 모드: Debug Mode (개발 단계) */
usb_hid_set_enabled(false);       /* HID OFF */
uart_router_set_cdc_enabled(true); /* CDC ON */
LOG_INF("Default mode: Debug (HID=OFF, CDC=ON)");
LOG_INF("Press SW0 to switch to Inventory Mode");
```

#### 동작 시나리오

```
┌─────────────────────────────────────────────────────────────────┐
│                         부팅                                     │
│  - Debug Mode (기본값)                                           │
│  - HID: OFF, CDC: ON                                            │
│  - Shell 사용 가능                                               │
└─────────────────────────────────────────────────────────────────┘
                               │
                          SW0 버튼 누름
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Inventory Mode                                │
│  - E310 Tag Inventory 실행                                       │
│  - HID: ON → EPC 키보드 출력                                     │
│  - CDC: OFF → Shell 출력 차단                                    │
│  - 자동 로그아웃                                                  │
└─────────────────────────────────────────────────────────────────┘
                               │
                          SW0 버튼 누름
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Debug Mode                                  │
│  - E310 Inventory 중지                                           │
│  - HID: OFF → EPC 출력 차단                                      │
│  - CDC: ON → Shell 사용 가능                                     │
│  - 로그인 필요 (login 명령어)                                     │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. 파일 변경 요약

| 파일 | 변경 내용 |
|------|----------|
| `nucleo_h723zg_parp01.dts` | `hid_0` 노드 주석 해제 |
| `src/main.c` | HID 초기화 주석 해제, SW0 콜백에 USB 모드 전환 추가 |
| `src/usb_hid.h` | `usb_hid_set_enabled()`, `usb_hid_is_enabled()` 추가 |
| `src/usb_hid.c` | HID mute 기능, mute 체크 추가 |
| `src/uart_router.h` | `epc_filter_t` 필드, CDC mute API 추가 |
| `src/uart_router.c` | HID include, EPC 필터, CDC mute, Shell 명령어 |

---

## 4. 설정 값

| 항목 | 기본값 | 범위 | 설명 |
|------|--------|------|------|
| `EPC_CACHE_SIZE` | 32 | - | 캐시할 수 있는 최대 EPC 수 |
| `EPC_DEBOUNCE_DEFAULT_SEC` | 3 | 1-60 | 기본 debounce 시간 (초) |
| HID Typing Speed | 600 CPM | 100-1500 | 타이핑 속도 (분당 문자수) |
| **HID 기본 상태** | **OFF** | ON/OFF | 개발 단계: OFF |
| **CDC 기본 상태** | **ON** | ON/OFF | 개발 단계: ON |
| **부팅 기본 모드** | **Debug** | Debug/Inventory | SW0으로 전환 |

---

## 5. Shell 명령어 요약

### 기존 명령어

| 명령어 | 설명 |
|--------|------|
| `hid speed [CPM]` | 타이핑 속도 조회/설정 (100-1500) |
| `hid test` | 테스트 EPC 전송 |
| `hid status` | HID 상태 표시 |

### 추가 명령어

| 명령어 | 설명 |
|--------|------|
| `usb hid <on\|off>` | HID 출력 On/Off |
| `usb cdc <on\|off>` | CDC 출력 On/Off |
| `usb status` | USB 상태 표시 |
| `hid debounce [sec]` | Debounce 시간 조회/설정 |
| `hid clear` | EPC 캐시 초기화 |

---

## 6. SW0 모드 전환 다이어그램

```
┌─────────────────────────────────────────────────────────────────┐
│                         부팅                                     │
│  - Debug Mode (기본값)                                           │
│  - HID: OFF, CDC: ON                                            │
│  - Inventory: OFF                                                │
│  - Shell 사용 가능 (login 필요)                                  │
└─────────────────────────────────────────────────────────────────┘
                               │
                          SW0 버튼 누름
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Inventory Mode                                │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ 1. shell_login_force_logout()   - 보안 로그아웃          │    │
│  │ 2. usb_hid_set_enabled(true)    - HID ON               │    │
│  │ 3. uart_router_set_cdc_enabled(false) - CDC OFF        │    │
│  │ 4. uart_router_start_inventory() - E310 시작           │    │
│  └─────────────────────────────────────────────────────────┘    │
│  - RFID 태그 읽기 → HID 키보드로 EPC 출력                        │
│  - Shell 사용 불가 (CDC OFF)                                     │
└─────────────────────────────────────────────────────────────────┘
                               │
                          SW0 버튼 누름
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Debug Mode                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ 1. uart_router_stop_inventory()  - E310 중지           │    │
│  │ 2. usb_hid_set_enabled(false)    - HID OFF             │    │
│  │ 3. uart_router_set_cdc_enabled(true) - CDC ON          │    │
│  └─────────────────────────────────────────────────────────┘    │
│  - Shell 사용 가능 (login 필요)                                  │
│  - 설정 변경 가능 (e310 power, hid speed 등)                     │
└─────────────────────────────────────────────────────────────────┘
```

---

## 7. EPC 처리 흐름

```
┌─────────────────────────────────────────────────────────────────┐
│                    RFID 태그 읽기                                │
└─────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                  E310 Auto-Upload 응답                           │
│                    (EPC + RSSI 데이터)                           │
└─────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                  EPC 중복 필터 체크                               │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ 캐시에서 동일 EPC 검색                                    │    │
│  │   - 발견 & debounce 시간 내 → 차단 (무시)                 │    │
│  │   - 발견 & debounce 시간 경과 → 허용 (타임스탬프 갱신)     │    │
│  │   - 미발견 → 허용 (캐시에 추가)                           │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                               │
                    ┌──────────┴──────────┐
                    │                     │
                허용 (Pass)            차단 (Block)
                    │                     │
                    ▼                     ▼
┌─────────────────────────────┐   ┌─────────────────────────────┐
│  USB HID 키보드로 전송       │   │  로그만 기록 (DEBUG 레벨)    │
│  - EPC 문자열 출력           │   │  "Duplicate EPC ignored"     │
│  - Enter 키 추가             │   │                              │
│  - 비프음 발생               │   │                              │
│  - 타이핑 속도(CPM) 적용     │   │                              │
└─────────────────────────────┘   └─────────────────────────────┘
```

---

## 8. 타수 제어 참고

현재 구현된 CPM(Characters Per Minute) 설정:

| CPM | 키 딜레이 | 분당 문자수 | 타수 표현 |
|-----|-----------|-------------|-----------|
| 100 | 300ms | 100자 | ~100타/분 |
| 200 | 150ms | 200자 | ~200타/분 |
| 300 | 100ms | 300자 | ~300타/분 |
| 600 | 50ms | 600자 | ~600타/분 (기본값) |
| 1000 | 30ms | 1000자 | ~1000타/분 |
| 1500 | 20ms | 1500자 | ~1500타/분 |

**설정 명령어**:
```bash
hid speed 100   # 100타/분 (느림)
hid speed 300   # 300타/분 (중간)
hid speed 600   # 600타/분 (기본값)
```

---

## 9. 메모리 영향 추정

| 항목 | 크기 | 설명 |
|------|------|------|
| `epc_cache_entry_t` | 72 bytes | EPC(62) + len(1) + timestamp(8) + padding |
| `epc_filter_t` | ~2.3 KB | entries(32*72) + count + next_idx + debounce_ms |

**예상 메모리 증가**:
- RAM: +2.5 KB (EPC 캐시)
- Flash: +1 KB (필터 로직)

---

## 10. 구현 순서

1. **Phase 1**: USB HID 활성화
   - Task 1.1: DTS 수정
   - Task 1.2: main.c 수정
   - Task 1.3: uart_router.c HID include
   - **빌드 테스트**

2. **Phase 2**: USB Mute 기능
   - Task 2.1: HID mute 변수/API 추가
   - Task 2.2: usb_hid_send_epc() mute 체크
   - Task 2.3: CDC mute 변수/API 추가
   - Task 2.4: uart_router_send_uart1() mute 체크
   - **빌드 테스트**

3. **Phase 3**: EPC 중복 필터링
   - Task 3.1: 구조체 정의
   - Task 3.2: API 구현
   - Task 3.3: 필터 로직 구현
   - Task 3.4: uart_router 통합
   - **빌드 테스트**

4. **Phase 4**: Shell 명령어
   - Task 4.1: USB on/off 명령어
   - Task 4.2: HID debounce 명령어
   - Task 4.3: HID clear 명령어
   - Task 4.4: HID status 업데이트
   - **빌드 테스트**

5. **Phase 5**: 인벤토리 연동
   - Task 5.1: 인벤토리 시작 시 캐시 초기화
   - **빌드 테스트**

6. **Phase 6**: SW0 모드 전환
   - Task 6.1: on_inventory_toggle() 콜백 수정
   - Task 6.2: 부팅 시 기본 모드 설정 (Debug)
   - **최종 빌드 및 테스트**

---

## 11. 승인 체크리스트

- [ ] 계획서 검토 완료
- [ ] Phase 1 시작 승인 (USB HID 활성화)
- [ ] Phase 2 시작 승인 (USB Mute 기능)
- [ ] Phase 3 시작 승인 (EPC 중복 필터링)
- [ ] Phase 4 시작 승인 (Shell 명령어)
- [ ] Phase 5 시작 승인 (인벤토리 연동)
- [ ] Phase 6 시작 승인 (SW0 모드 전환)
- [ ] 전체 완료 확인

---

**작성자**: Claude
**검토자**: (승인 대기)
