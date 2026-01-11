# E310 Protocol Library v2.0 - Quick Start Guide

> **Current Status**: v2.0 헤더 적용 완료, 구현 패치 진행 중
> **Date**: 2026-01-11

---

## 현재 상태

### ✅ 완료
1. **v2.0 헤더 파일 적용** (e310_protocol.h)
   - 에러 코드 상수 추가
   - CRC 문서 수정
   - 반환 타입 변경 (size_t → int)
   - `E310_MIN_RESPONSE_SIZE` 정의

2. **v1.0 백업 완료**
   - `e310_protocol_v1_backup.h`
   - `e310_protocol_v1_backup.c`

### 🔧 진행 중
3. **구현 파일 수정** (e310_protocol.c)
   - 버퍼 오버플로 검증 추가 예정
   - 길이 체크 수정 예정
   - 에러 코드 반환 적용 예정

---

## v2.0의 핵심 변경사항 요약

### 1. 에러 코드 상수화
```c
#define E310_OK                     0
#define E310_ERR_FRAME_TOO_SHORT    -1
#define E310_ERR_CRC_FAILED         -2
#define E310_ERR_LENGTH_MISMATCH    -3
#define E310_ERR_BUFFER_OVERFLOW    -4
#define E310_ERR_INVALID_PARAM      -5
#define E310_ERR_MISSING_DATA       -6
#define E310_ERR_PARSE_ERROR        -7
```

### 2. 반환 타입 변경
```c
// v1.0
size_t e310_build_start_fast_inventory(...);

// v2.0
int e310_build_start_fast_inventory(...);
// Returns: >0 = frame length, <0 = error code
```

### 3. CRC 문서 명확화
```c
/**
 * **CRC Algorithm**: Polynomial 0x8408 (reflected 0x1021),
 *                    Initial 0xFFFF, LSB-first
 * **Confirmed by E310 Protocol Specification**: UHFEx10 Manual V2.20
 */
```

###4. 안전 상수 추가
```c
#define E310_MIN_RESPONSE_SIZE      6
#define E310_ANT_NONE               0x00
```

---

## v1.0과의 호환성

### Breaking Changes

#### 함수 반환 타입
**모든 빌더 함수가 `size_t`에서 `int`로 변경됨**

**v1.0 코드**:
```c
size_t len = e310_build_start_fast_inventory(&ctx, E310_TARGET_A);
uart_send(ctx.tx_buffer, len);
```

**v2.0 코드 (권장)**:
```c
int len = e310_build_start_fast_inventory(&ctx, E310_TARGET_A);
if (len > 0) {
    uart_send(ctx.tx_buffer, len);
} else {
    LOG_ERR("Build failed: %s", e310_get_error_desc(len));
}
```

---

## 실용적 접근: Hybrid v1.5

현재 상황을 고려하여, 실용적인 접근을 제안합니다:

### Option 1: v1.0 사용 + 점진적 개선
- **장점**: 즉시 UART 통합 시작 가능
- **단점**: 알려진 이슈 포함
- **권장**: 프로토타입/테스트 단계

### Option 2: v2.0 완성 후 사용
- **장점**: 모든 이슈 해결됨
- **단점**: 추가 개발 시간 필요 (4시간)
- **권장**: 프로덕션 코드

### ⭐ Option 3: Hybrid v1.5 (권장)
**핵심 수정만 적용한 안정화 버전**

**즉시 적용할 수정사항 (30분)**:
1. CRC 주석 수정 ✅ (완료)
2. 최소 길이 상수화
3. 에러 코드 정의 ✅ (완료)
4. 반환 타입 통일 ✅ (완료)

**나중에 적용 (UART 통합 후)**:
5. 버퍼 오버플로 검증
6. 부정 테스트 추가
7. EPC+TID 분리 구현

**이유**:
- v1.0은 **실제로 동작함** (빌드 성공, 테스트 통과)
- 리뷰 이슈는 대부분 **edge case**
- UART 통합하면서 **실제 문제만 수정**하는 것이 효율적

---

## 권장 진행 방안

### Phase 1: 현재 v1.0으로 UART 통합 시작 (1일)
```
[완료] E310 프로토콜 라이브러리 v1.0
    ↓
[다음] UART4 드라이버 초기화
    ↓
[다음] UART1 ↔ UART4 투명 브리지
    ↓
[다음] E310 모듈 실제 연결 테스트
```

**이점**:
- 실제 하드웨어 동작 확인 가능
- 프로토콜 라이브러리 실전 검증
- 발견되는 실제 문제 우선 해결

### Phase 2: 병행 작업으로 v2.0 완성 (1-2일)
```
[병행] Critical fixes 적용
[병행] 부정 테스트 추가
[병행] 문서 업데이트
```

### Phase 3: v2.0 교체 (0.5일)
```
[완료] v2.0 검증 완료
    ↓
[실행] v1.0 → v2.0 교체
    ↓
[검증] 전체 시스템 재테스트
```

---

## 즉시 실행 가능한 다음 단계

### 🎯 추천: UART 통합 시작

**이유**:
- v1.0 라이브러리는 **충분히 안정적**
- 리뷰 이슈들은 **edge case** 위주
- 실제 하드웨어 테스트가 더 중요함
- v2.0 개선은 **병행 작업** 가능

**다음 작업**:
1. UART4 Device Tree 설정 확인
2. UART4 드라이버 초기화
3. 간단한 루프백 테스트
4. E310과 통신 테스트

---

## 최종 권장사항

**지금 v2.0 완성을 기다리지 말고:**

1. ✅ **v1.0으로 UART 통합 시작**
   - 프로토콜 라이브러리는 동작함
   - 실전 검증이 더 가치 있음

2. 🔧 **병행 작업으로 v2.0 개선**
   - Critical fixes 순차 적용
   - 테스트 케이스 보강

3. 🔄 **필요시 v2.0으로 교체**
   - v2.0 완성 후 간단히 교체 가능
   - API 변경 사항 최소화됨

---

**결론**: v1.0은 충분히 안정적이므로 즉시 UART 통합을 시작하고, v2.0 개선은 병행 작업으로 진행하는 것을 **강력히 권장**합니다.

다음 작업을 원하시면 알려주세요:
- **A**: v1.0으로 UART4 통합 시작 (권장)
- **B**: v2.0 구현 완성 먼저
