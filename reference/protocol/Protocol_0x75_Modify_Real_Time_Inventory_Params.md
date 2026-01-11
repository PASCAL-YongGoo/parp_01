### 0x75 Modify Parameters of Real Time Inventory Mode Command

이 명령어는 '실시간 인벤토리 모드'와 관련된 다양한 파라미터를 수정하는 데 사용됩니다. 실시간 인벤토리 모드는 리더기가 독립적으로 태그를 지속해서 스캔하고 보고하는 자동화된 동작 모드입니다.

#### 1. 요청 프레임 (PC → E310)

**프레임 구조:**

| Len | Adr | Cmd | Data[] | CRC-16 (LSB) | CRC-16 (MSB) |
| :-- | :-- | :-- | :-- | :--- | :--- |
| 가변 | 1 | 1 | 가변 | 1 | 1 |

**필드 설명:**

| 필드 | 길이 (Byte) | 설명 |
| :--- | :--- | :--- |
| `Len` | 1 | `Adr`부터 `CRC-16` MSB까지의 총 바이트 수. |
| `Adr` | 1 | 리더기 주소 (0x00 ~ 0xFE). 0xFF는 브로드캐스트. |
| `Cmd` | 1 | 명령어 코드. `0x75`으로 고정. |
| `Data[]`| 가변 | 실시간 인벤토리 파라미터. 아래 "Data[] 필드 포맷" 참조. |
| `CRC-16`| 2 | `Len`부터 `Data[]`까지의 CRC-16 체크섬. |

**Data[] 필드 포맷:**

| TagProtocol | ReadPauseTime | FliterTime | QValue | Session | MaskMem | MaskAdr | MaskLen | MaskData | AdrTID | LenTID |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1 | 1 | 1 | 1 | 1 | 2 | 1 | 가변 | 1 | 1 |

**Data[] 파라미터 설명:**

| 파라미터 | 길이(Byte) | 설명 |
| :--- | :--- | :--- |
| `TagProtocol`| 1 | 태그 프로토콜 정의 (0: EPC C1G2, 1: ISO18000-6B). |
| `ReadPauseTime`| 1 | 두 실시간 인벤토리 사이의 휴식 시간 (0x00: 10ms, 0x01: 20ms, ...). |
| `FliterTime` | 1 | 태그 필터링 시간 (0~255초). 이 시간 내에는 동일 태그 정보를 한 번만 업로드. 0은 필터링 비활성화. |
| `QValue` | 1 | Q-value (Anti-collision). |
| `Session` | 1 | Session ID (S0-S3). |
| `Mask...` | 가변 | 특정 태그 그룹을 필터링하기 위한 마스크 파라미터. |
| `AdrTID` | 1 | TID 인벤토리 시작 주소. |
| `LenTID` | 1 | TID 인벤토리 데이터 길이. |

---

#### 2. 응답 프레임 (E310 → PC)

**프레임 구조:**

| Len | Adr | reCmd | Status | Data[] | CRC-16 (LSB) | CRC-16 (MSB) |
| :-- | :-- | :--- | :--- | :--- | :--- | :--- |
| 5 | 1 | 1 | 1 | 0 | 1 | 1 |

**필드 설명:**

| 필드 | 길이 (Byte) | 설명 |
| :--- | :--- | :--- |
| `Len` | 1 | `0x05`. |
| `Adr` | 1 | 리더기 주소. |
| `reCmd` | 1 | 응답하는 명령어 코드. `0x75`으로 고정. |
| `Status`| 1 | 명령어 실행 상태. 성공 시 `0x00`. |
| `Data[]`| 0 | 데이터 없음. |
| `CRC-16`| 2 | `Len`부터 `Status`까지의 CRC-16 체크섬. |
