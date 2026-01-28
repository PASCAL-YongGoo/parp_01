# PARP-01 개선 및 프로토콜 구현 계획

> 작성일: 2026-01-28
> 버전: 2.0

---

## 목차

1. [Part 1: 소스코드 개선 계획](#part-1-소스코드-개선-계획)
2. [Part 2: E310 프로토콜 완전 구현 계획](#part-2-e310-프로토콜-완전-구현-계획)
3. [통합 일정](#통합-일정)

---

# Part 1: 소스코드 개선 계획

## 1.1 발견된 문제점 요약

| ID | 파일 | 라인 | 심각도 | 문제 설명 |
|----|------|------|--------|-----------|
| BUG-001 | uart_router.c | 50, 98 | 🔴 높음 | TX 콜백에서 RX 링버퍼 잘못 사용 |
| BUG-002 | e310_protocol.c | 387 | 🟡 중간 | reader_info 파싱 인덱스 오류 |
| BUG-003 | e310_protocol.c | 256-275 | 🟡 중간 | EPC+TID 분리 파싱 미구현 |
| PERF-001 | usb_hid.c | 223-241 | 🟡 중간 | 블로킹 딜레이로 성능 저하 |
| PERF-002 | main.c | 130 | 🟢 낮음 | 비효율적 시간 계산 로직 |
| DESIGN-001 | uart_router.c | - | 🟡 중간 | TX 전용 링버퍼 부재 |
| DESIGN-002 | e310_protocol.h | - | 🟢 낮음 | 에러 설명 함수 미구현 |

---

## 1.2 Phase 1: 버그 수정 (Critical)

### Task 1.1: UART Router TX 버퍼 버그 수정

**문제:** `uart1_callback`과 `uart4_callback`의 TX 처리에서 RX 링버퍼를 읽고 있음

**현재 코드 (uart_router.c:48-64):**
```c
if (uart_irq_tx_ready(dev)) {
    uint8_t buf[64];
    int len = ring_buf_get(&router->uart1_rx_ring, buf, sizeof(buf));  // ❌ 잘못됨!
    ...
}
```

**수정 방안:**
1. TX 전용 링버퍼 추가
2. TX 콜백에서 TX 링버퍼 사용
3. `uart_router_send_*` 함수에서 TX 링버퍼에 데이터 추가

**수정할 파일:**
- `src/uart_router.h` - TX 링버퍼 필드 추가
- `src/uart_router.c` - TX 로직 전면 수정

**예상 변경량:** ~100 LOC

---

### Task 1.2: Reader Info 파싱 인덱스 수정

**문제:** `e310_parse_reader_info()`에서 `check_antenna` 인덱스가 잘못됨

**현재 코드 (e310_protocol.c:386-387):**
```c
/* data[9], data[10] = reserved */
info->check_antenna = data[11];  // ❌ data[12]가 맞음 (13바이트 기준)
```

**프로토콜 문서 기준:**
```
| Version(2) | Type(1) | Tr_Type(1) | dmaxfre(1) | dminfre(1) | Power(1) | Scntm(1) | Ant(1) | Reserved(2) | CheckAnt(1) |
| [0-1]      | [2]     | [3]        | [4]        | [5]        | [6]      | [7]      | [8]    | [9-10]      | [11]        |
```

**결론:** 현재 코드가 맞음 (재검토 결과). 단, 길이 체크 `length < 13`은 `length < 12`로 수정 필요.

**수정할 파일:**
- `src/e310_protocol.c` - 길이 체크 수정

**예상 변경량:** ~5 LOC

---

### Task 1.3: EPC+TID 분리 파싱 구현

**문제:** `e310_parse_tag_data()`에서 EPC+TID 결합 데이터 파싱 미완성

**현재 코드 (e310_protocol.c:257-275):**
```c
if (epc_tid_combined) {
    /* TODO: Implement proper EPC+TID separation if needed */
    tag->epc_len = data_bytes;  // TID 무시됨
    ...
}
```

**수정 방안:**
E310 프로토콜에서 EPC+TID 결합 시 TID 길이는 별도 파라미터로 전달됨 (0x01 명령의 `LenTID`).
응답에서는 EPC 뒤에 TID가 연속으로 붙음.

```c
if (epc_tid_combined) {
    // EPC는 (data_bytes - tid_len) 바이트
    // TID는 tid_len 바이트 (호출자가 제공해야 함)
    // 또는 프로토콜 문서 재확인 필요
}
```

**수정할 파일:**
- `src/e310_protocol.h` - 파싱 함수 시그니처 수정 (tid_len 파라미터 추가)
- `src/e310_protocol.c` - EPC+TID 분리 로직 구현

**예상 변경량:** ~50 LOC

---

## 1.3 Phase 2: 성능 개선

### Task 2.1: USB HID 비동기 전송 구현

**문제:** 문자마다 `k_msleep(20)` 호출로 100자 전송 시 2초+ 소요

**현재 코드 (usb_hid.c:209-242):**
```c
for (size_t i = 0; i < len; i++) {
    // Key press
    hid_device_submit_report(...);
    k_msleep(20);  // ❌ 블로킹
    // Key release
    hid_device_submit_report(...);
    k_msleep(20);  // ❌ 블로킹
}
```

**수정 방안 A: 타이머 기반 비동기 전송**
```c
struct hid_send_ctx {
    const uint8_t *data;
    size_t len;
    size_t idx;
    bool key_pressed;
    struct k_timer timer;
};

static void hid_send_timer_handler(struct k_timer *timer) {
    // 상태 머신으로 key press/release 처리
}
```

**수정 방안 B: 워크큐 기반 전송**
```c
static void hid_send_work_handler(struct k_work *work) {
    // 작업 항목으로 처리
}
```

**권장:** 방안 A (타이머 기반) - 정확한 타이밍 제어 가능

**수정할 파일:**
- `src/usb_hid.h` - 비동기 API 추가
- `src/usb_hid.c` - 타이머 기반 전송 구현

**예상 변경량:** ~150 LOC

---

### Task 2.2: 메인 루프 타이밍 개선

**문제:** 비효율적인 시간 계산 로직

**현재 코드 (main.c:130):**
```c
if (k_uptime_get() - (int64_t)count * 500 >= 500) {
    ...
}
```

**수정 방안:** k_timer 사용
```c
static struct k_timer led_timer;
static volatile bool led_toggle_flag = false;

void led_timer_handler(struct k_timer *timer) {
    led_toggle_flag = true;
}

int main(void) {
    k_timer_init(&led_timer, led_timer_handler, NULL);
    k_timer_start(&led_timer, K_MSEC(500), K_MSEC(500));

    while (1) {
        if (led_toggle_flag) {
            led_toggle_flag = false;
            // LED toggle
        }
        uart_router_process(&uart_router);
        k_msleep(1);  // yield
    }
}
```

**수정할 파일:**
- `src/main.c` - 타이머 기반 LED 제어

**예상 변경량:** ~30 LOC

---

## 1.4 Phase 3: 설계 개선

### Task 3.1: 에러 설명 함수 구현

**문제:** `e310_get_error_desc()` 함수 선언만 있고 구현 없음

**수정할 파일:**
- `src/e310_protocol.c` - 함수 구현 추가

```c
const char *e310_get_error_desc(int error_code)
{
    switch (error_code) {
    case E310_OK:                  return "Success";
    case E310_ERR_FRAME_TOO_SHORT: return "Frame too short";
    case E310_ERR_CRC_FAILED:      return "CRC verification failed";
    case E310_ERR_LENGTH_MISMATCH: return "Length field mismatch";
    case E310_ERR_BUFFER_OVERFLOW: return "Buffer overflow";
    case E310_ERR_INVALID_PARAM:   return "Invalid parameter";
    case E310_ERR_MISSING_DATA:    return "Missing required data";
    case E310_ERR_PARSE_ERROR:     return "Parse error";
    default:                       return "Unknown error";
    }
}
```

**예상 변경량:** ~20 LOC

---

### Task 3.2: 테스트 코드 강화

**현재 상태:** `e310_test.c`에 기본 테스트만 존재

**추가할 테스트:**
1. CRC 에러 케이스 테스트
2. 버퍼 오버플로우 테스트
3. 경계값 테스트 (최대 EPC 길이, 최대 프레임 크기)
4. 응답 파싱 에러 케이스

**수정할 파일:**
- `src/e310_test.c` - 테스트 케이스 추가

**예상 변경량:** ~200 LOC

---

# Part 2: E310 프로토콜 완전 구현 계획

## 2.1 구현 우선순위 정의

### Tier 1: 핵심 기능 (Must Have)
| Cmd | 명령어 | 용도 |
|-----|--------|------|
| 0x02 | Read Data | 태그 메모리 읽기 |
| 0x03 | Write Data | 태그 메모리 쓰기 |
| 0x2F | Modify RF Power | RF 출력 조정 |
| 0x9A | Select Command | 특정 태그 선택 |

### Tier 2: 중요 기능 (Should Have)
| Cmd | 명령어 | 용도 |
|-----|--------|------|
| 0x04 | Write EPC | EPC 직접 쓰기 |
| 0x0F | Single Tag Inventory | 단일 태그 감지 |
| 0x4C | Obtain Reader SN | 리더 식별 |
| 0x72 | Get Data From Buffer | 버퍼 데이터 조회 |
| 0x73 | Clear Memory Buffer | 버퍼 초기화 |
| 0x74 | Get Tag Count | 태그 카운트 |
| 0x92 | Measure Temperature | 온도 모니터링 |

### Tier 3: 확장 기능 (Nice to Have)
| Cmd | 명령어 | 용도 |
|-----|--------|------|
| 0x22 | Modify Frequency | 주파수 설정 |
| 0x25 | Modify Inventory Time | 인벤토리 시간 |
| 0x3F | Setup Antenna Mux | 안테나 멀티플렉서 |
| 0x10 | Block Writing | 블록 쓰기 |
| 0x15 | Extended Data Reading | 확장 읽기 |
| 0x16 | Extended Data Writing | 확장 쓰기 |

### Tier 4: 고급 기능 (Optional)
- 보호/잠금 관련 (0x05-0x0B)
- EAS 관련 (0x0C-0x0D)
- Monza4QT 관련 (0x11-0x12)
- 기타 설정 (0x66-0x7F, 0x91, 0xEA-0xEB)

---

## 2.2 Phase 1: Tier 1 핵심 기능 구현

### Task P1.1: Read Data (0x02) 구현

**요청 프레임:**
```
| Len | Adr | Cmd(0x02) | ENum | EPC | Mem | WordPtr | Num | Pwd | CRC |
```

**구현 항목:**
1. `e310_build_read_data()` - 요청 프레임 생성
2. `e310_parse_read_data_response()` - 응답 파싱
3. 데이터 구조체 정의

**API 설계:**
```c
// 요청 파라미터
typedef struct {
    uint8_t epc[E310_MAX_EPC_LENGTH];
    uint8_t epc_len;
    uint8_t mem_bank;       // 0x00-0x03
    uint8_t word_ptr;       // 시작 주소
    uint8_t word_count;     // 읽을 워드 수 (1-120)
    uint8_t password[4];    // 접근 암호
    // 마스크 옵션 (선택)
    bool use_mask;
    uint8_t mask_mem;
    uint16_t mask_addr;
    uint8_t mask_len;
    uint8_t mask_data[E310_MAX_MASK_LENGTH];
} e310_read_params_t;

// 응답 데이터
typedef struct {
    uint8_t data[240];      // 최대 120 워드 = 240 바이트
    uint8_t word_count;
    uint8_t status;
} e310_read_response_t;

// API
int e310_build_read_data(e310_context_t *ctx, const e310_read_params_t *params);
int e310_parse_read_data_response(const uint8_t *data, size_t length,
                                   e310_read_response_t *response);
```

**예상 변경량:** ~200 LOC

---

### Task P1.2: Write Data (0x03) 구현

**요청 프레임:**
```
| Len | Adr | Cmd(0x03) | WNum | ENum | EPC | Mem | WordPtr | Data | Pwd | CRC |
```

**API 설계:**
```c
typedef struct {
    uint8_t epc[E310_MAX_EPC_LENGTH];
    uint8_t epc_len;
    uint8_t mem_bank;
    uint8_t word_ptr;
    uint8_t data[240];
    uint8_t word_count;
    uint8_t password[4];
} e310_write_params_t;

int e310_build_write_data(e310_context_t *ctx, const e310_write_params_t *params);
int e310_parse_write_data_response(const uint8_t *data, size_t length, uint8_t *status);
```

**예상 변경량:** ~180 LOC

---

### Task P1.3: Modify RF Power (0x2F) 구현

**요청 프레임:**
```
| Len(0x05) | Adr | Cmd(0x2F) | Power | CRC |
```

**API 설계:**
```c
// Power: 0-30 (dBm)
int e310_build_modify_rf_power(e310_context_t *ctx, uint8_t power);
int e310_parse_modify_rf_power_response(const uint8_t *data, size_t length, uint8_t *status);
```

**예상 변경량:** ~50 LOC

---

### Task P1.4: Select Command (0x9A) 구현

**요청 프레임:**
```
| Len | Adr | Cmd(0x9A) | SelParam | Truncate | Target | Action | MemBank | Pointer | Length | Mask | CRC |
```

**API 설계:**
```c
typedef struct {
    uint8_t sel_param;      // Select 파라미터
    uint8_t truncate;       // Truncation 설정
    uint8_t target;         // Target (A/B)
    uint8_t action;         // Action 코드
    uint8_t mem_bank;       // 메모리 뱅크
    uint16_t pointer;       // 비트 포인터
    uint8_t length;         // 마스크 길이
    uint8_t mask[E310_MAX_MASK_LENGTH];
} e310_select_params_t;

int e310_build_select(e310_context_t *ctx, const e310_select_params_t *params);
int e310_parse_select_response(const uint8_t *data, size_t length, uint8_t *status);
```

**예상 변경량:** ~150 LOC

---

## 2.3 Phase 2: Tier 2 중요 기능 구현

### Task P2.1: Write EPC (0x04)
```c
typedef struct {
    uint8_t old_epc[E310_MAX_EPC_LENGTH];
    uint8_t old_epc_len;
    uint8_t new_epc[E310_MAX_EPC_LENGTH];
    uint8_t new_epc_len;
    uint8_t password[4];
} e310_write_epc_params_t;

int e310_build_write_epc(e310_context_t *ctx, const e310_write_epc_params_t *params);
```
**예상 변경량:** ~100 LOC

### Task P2.2: Single Tag Inventory (0x0F)
```c
int e310_build_single_tag_inventory(e310_context_t *ctx);
```
**예상 변경량:** ~50 LOC

### Task P2.3: Obtain Reader SN (0x4C)
```c
typedef struct {
    char serial_number[32];
} e310_reader_sn_t;

int e310_build_obtain_reader_sn(e310_context_t *ctx);
int e310_parse_reader_sn(const uint8_t *data, size_t length, e310_reader_sn_t *sn);
```
**예상 변경량:** ~60 LOC

### Task P2.4: Buffer Commands (0x72, 0x73, 0x74)
```c
int e310_build_get_data_from_buffer(e310_context_t *ctx);
int e310_build_clear_memory_buffer(e310_context_t *ctx);
int e310_build_get_tag_count(e310_context_t *ctx);
int e310_parse_tag_count(const uint8_t *data, size_t length, uint32_t *count);
```
**예상 변경량:** ~120 LOC

### Task P2.5: Measure Temperature (0x92)
```c
typedef struct {
    int8_t temperature;     // 섭씨 온도
} e310_temperature_t;

int e310_build_measure_temperature(e310_context_t *ctx);
int e310_parse_temperature(const uint8_t *data, size_t length, e310_temperature_t *temp);
```
**예상 변경량:** ~50 LOC

---

## 2.4 Phase 3: Tier 3 확장 기능 구현

### Task P3.1: Modify Frequency (0x22)
**예상 변경량:** ~80 LOC

### Task P3.2: Modify Inventory Time (0x25)
**예상 변경량:** ~50 LOC

### Task P3.3: Setup Antenna Mux (0x3F)
**예상 변경량:** ~100 LOC

### Task P3.4: Block Writing (0x10)
**예상 변경량:** ~150 LOC

### Task P3.5: Extended Read/Write (0x15, 0x16)
**예상 변경량:** ~200 LOC

---

## 2.5 Phase 4: Tier 4 고급 기능 구현 (선택)

필요 시 구현:
- Kill Tag (0x05)
- Set Protection (0x06)
- Block Erase (0x07)
- Read Protection 관련 (0x08-0x0B)
- EAS 관련 (0x0C-0x0D)
- 기타 설정 명령어들

---

## 2.6 구현 가이드라인

### 코드 스타일
```c
/**
 * @brief [함수 설명]
 *
 * @param ctx Context
 * @param params 파라미터 구조체
 * @return 프레임 길이 (성공), 음수 에러 코드 (실패)
 */
int e310_build_xxx(e310_context_t *ctx, const e310_xxx_params_t *params)
{
    // 1. 파라미터 검증
    if (!ctx || !params) {
        return E310_ERR_INVALID_PARAM;
    }

    // 2. 데이터 길이 계산
    size_t data_len = ...;

    // 3. 버퍼 오버플로우 체크
    if (3 + data_len + 2 > E310_MAX_FRAME_SIZE) {
        return E310_ERR_BUFFER_OVERFLOW;
    }

    // 4. 프레임 헤더 빌드
    size_t idx = e310_build_frame_header(ctx, E310_CMD_XXX, data_len);

    // 5. 데이터 필드 추가
    ctx->tx_buffer[idx++] = ...;

    // 6. CRC 추가 및 반환
    return e310_finalize_frame(ctx, idx);
}
```

### 테스트 템플릿
```c
static void test_build_xxx(void)
{
    LOG_INF("=== Testing XXX Command ===");

    e310_context_t ctx;
    e310_init(&ctx, E310_ADDR_DEFAULT);

    e310_xxx_params_t params = {
        // 테스트 파라미터
    };

    int len = e310_build_xxx(&ctx, &params);

    print_hex_dump("XXX Command", ctx.tx_buffer, len);
    LOG_INF("Frame length: %d bytes", len);

    // CRC 검증
    int crc_result = e310_verify_crc(ctx.tx_buffer, len);
    LOG_INF("CRC verification: %s", (crc_result == E310_OK) ? "PASS" : "FAIL");
}
```

---

# 통합 일정

## 마일스톤 정의

| 마일스톤 | 내용 | 예상 LOC |
|----------|------|----------|
| M1 | 버그 수정 완료 | ~175 LOC |
| M2 | 성능 개선 완료 | ~180 LOC |
| M3 | 설계 개선 완료 | ~220 LOC |
| M4 | Tier 1 프로토콜 완료 | ~580 LOC |
| M5 | Tier 2 프로토콜 완료 | ~380 LOC |
| M6 | Tier 3 프로토콜 완료 | ~580 LOC |
| **총합** | | **~2,115 LOC** |

---

## 작업 순서 (권장)

### Stage 1: Critical Fixes
1. ✅ Task 1.1: UART Router TX 버퍼 버그 수정
2. ✅ Task 1.2: Reader Info 파싱 수정
3. ✅ Task 1.3: EPC+TID 파싱 구현

### Stage 2: Core Protocol
4. ✅ Task P1.1: Read Data (0x02)
5. ✅ Task P1.2: Write Data (0x03)
6. ✅ Task P1.3: Modify RF Power (0x2F)
7. ✅ Task P1.4: Select Command (0x9A)

### Stage 3: Performance & Testing
8. ✅ Task 2.1: USB HID 비동기 전송
9. ✅ Task 2.2: 메인 루프 타이밍 개선
10. ✅ Task 3.2: 테스트 코드 강화

### Stage 4: Extended Protocol
11. ✅ Task P2.1-P2.5: Tier 2 명령어들
12. ✅ Task P3.1-P3.5: Tier 3 명령어들

### Stage 5: Optional
13. ⬜ Tier 4 고급 기능 (필요 시)

---

## 파일 구조 예상 변경

```
src/
├── main.c                  # 수정 (타이머 기반)
├── e310_protocol.h         # 확장 (새 구조체, API 추가)
├── e310_protocol.c         # 확장 (새 명령어 구현)
├── e310_protocol_ext.c     # 신규 (Tier 3-4 명령어)
├── e310_test.c             # 확장 (테스트 케이스 추가)
├── uart_router.h           # 수정 (TX 버퍼 추가)
├── uart_router.c           # 수정 (TX 로직 수정)
├── usb_hid.h               # 수정 (비동기 API)
├── usb_hid.c               # 수정 (타이머 기반 전송)
└── usb_device.c            # 변경 없음
```

---

## 검증 체크리스트

### 각 명령어 구현 완료 기준
- [ ] 빌더 함수 구현
- [ ] 파서 함수 구현 (해당 시)
- [ ] 테스트 코드 작성
- [ ] CRC 검증 통과
- [ ] 프레임 길이 검증
- [ ] 문서 주석 완료

### 최종 검증
- [ ] 전체 빌드 성공
- [ ] 모든 테스트 통과
- [ ] 실제 E310 하드웨어 연동 테스트
- [ ] 메모리 사용량 확인 (Flash < 200KB, RAM < 50KB)

---

## 참고 문서

- `reference/protocol/` - E310 프로토콜 명세서 (58개)
- `docs/E310_LIBRARY.md` - 라이브러리 사용 가이드
- UHFEx10 User Manual V2.20 - 공식 매뉴얼

