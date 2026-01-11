### 0x77 Load Real Time Inventory Mode Parameters Command

이 명령어는 리더기에 현재 설정된 '실시간 인벤토리 모드'의 모든 파라미터를 조회하는 데 사용됩니다. `0x75` 명령어로 설정한 값들을 읽어오는 역할을 합니다.

#### 1. 요청 프레임 (PC → E310)

**프레임 구조:**

| Len | Adr | Cmd | CRC-16 (LSB) | CRC-16 (MSB) |
| :-- | :-- | :-- | :--- | :--- |
| 4 | 1 | 1 | 1 | 1 |

**필드 설명:**

| 필드 | 길이 (Byte) | 설명 |
| :--- | :--- | :--- |
| `Len` | 1 | `0x04`. |
| `Adr` | 1 | 리더기 주소 (0x00 ~ 0xFE). 0xFF는 브로드캐스트. |
| `Cmd` | 1 | 명령어 코드. `0x77`으로 고정. |
| `CRC-16`| 2 | `Len`부터 `Cmd`까지의 CRC-16 체크섬. |

---

#### 2. 응답 프레임 (E310 → PC)

**프레임 구조:**

| Len | Adr | reCmd | Status | Data[] | CRC-16 (LSB) | CRC-16 (MSB) |
| :-- | :-- | :--- | :--- | :--- | :--- | :--- |
| 가변 | 1 | 1 | 1 | 가변 | 1 | 1 |

**필드 설명:**

| 필드 | 길이 (Byte) | 설명 |
| :--- | :--- | :--- |
| `Len` | 1 | `Adr`부터 `CRC-16` MSB까지의 총 바이트 수. |
| `Adr` | 1 | 리더기 주소. |
| `reCmd` | 1 | 응답하는 명령어 코드. `0x77`으로 고정. |
| `Status`| 1 | 명령어 실행 상태. 성공 시 `0x00`. |
| `Data[]`| 가변 | 현재 설정된 실시간 인벤토리 파라미터. 아래 "Data[] 필드 포맷" 참조. |
| `CRC-16`| 2 | `Len`부터 `Data[]`까지의 CRC-16 체크섬. |

**Data[] 필드 포맷:**
`0x75` 명령어로 설정 가능한 모든 파라미터를 포함합니다.

| ReadMode | TagProtocol | ReadPauseTime | FliterTime | QValue | Session | MaskMem | MaskAdr | MaskLen | MaskData | AdrTID | LenTID |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 1 | 32 | 2 | 1 |

**Data[] 파라미터 설명:**

| 파라미터 | 길이(Byte) | 설명 |
| :--- | :--- | :--- |
| `ReadMode` | 1 | 현재 동작 모드 (0: 응답, 1: 실시간, 2: 트리거 기반 실시간). |
| `TagProtocol`| 1 | 태그 프로토콜 정의 (0: EPC C1G2, 1: ISO18000-6B). |
| `ReadPauseTime`| 1 | 인벤토리 간 휴식 시간. |
| `FliterTime` | 1 | 태그 필터링 시간. |
| `QValue` | 1 | Q-value. |
| `Session` | 1 | Session ID. |
| `MaskMem` | 1 | 마스크 메모리 영역. |
| `MaskAdr` | 2 | 마스크 시작 주소. |
| `MaskLen` | 1 | 마스크 비트 길이. |
| `MaskData` | 32 | 마스크 데이터 (32바이트 고정 길이). |
| `AdrTID` | 2 | TID 인벤토리 시작 주소. |
| `LenTID` | 1 | TID 인벤토리 데이터 길이. |
