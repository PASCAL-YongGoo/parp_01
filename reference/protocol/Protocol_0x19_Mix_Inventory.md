### 0x19 Mix Inventory Command

`Mix Inventory`는 가장 복합적인 인벤토리 명령어로, 태그를 식별하는 동시에 지정된 메모리 영역의 데이터를 읽어오는 동작을 함께 수행합니다. 즉, 인벤토리(Inventory)와 데이터 읽기(Read Data)가 결합된 기능입니다.

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
| `Cmd` | 1 | 명령어 코드. `0x19`으로 고정. |
| `Data[]`| 가변 | 인벤토리 및 읽기 파라미터. 아래 "Data[] 필드 포맷" 참조. |
| `CRC-16`| 2 | `Len`부터 `Data[]`까지의 CRC-16 체크섬. |

**Data[] 필드 포맷:**
`Mix Inventory`의 `Data` 필드는 인벤토리 파라미터와 읽기 파라미터를 모두 포함합니다.

| QValue | Session | MaskMem | MaskAdr | MaskLen | MaskData | ReadMem | ReadAdr | ReadLen | Pwd | Target | Ant | Scantime |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :-- | :--- | :- | :--- |
| 1 | 1 | 1 | 2 | 1 | 가변 | 1 | 2 | 1 | 4 | 1 | 1 | 1 |

**Data[] 파라미터 설명:**

| 파라미터 | 길이(Byte) | 설명 |
| :--- | :--- | :--- |
| `QValue` | 1 | Q-value (Anti-collision) |
| `Session` | 1 | Session ID (S0-S3) |
| `Mask...` | 가변 | 특정 태그 그룹을 필터링하기 위한 마스크 파라미터 |
| `ReadMem` | 1 | 읽을 메모리 영역 (0x00: Reserved, 0x01: EPC, 0x02: TID, 0x03: User) |
| `ReadAdr` | 2 | 읽기 시작할 워드 주소 |
| `ReadLen` | 1 | 읽을 데이터의 워드 수 (최대 120) |
| `Pwd` | 4 | 태그 접근 암호 |
| `Target` | 1 | 인벤토리 대상 (A/B) |
| `Ant` | 1 | 사용할 안테나 |
| `Scantime` | 1 | 최대 스캔 시간 |

---

#### 2. 응답 프레임 (E310 → PC)

응답은 두 가지 종류가 있습니다. 하나는 인벤토리 후 통계 데이터를 반환하는 것이고, 다른 하나는 실제 태그 데이터를 패킷 형태로 반환하는 것입니다.

**1) 통계 데이터 응답 (Status = 0x26)**
인벤토리 사이클이 끝난 후 통계 정보를 요약하여 보냅니다.

| Len | Adr | reCmd | Status | Ant | ReadRate | TotalCount | CRC-16 |
| :-- | :-- | :--- | :--- | :- | :--- | :--- | :--- |
| 가변 | 1 | 1 | 1 | 1 | 2 | 4 | 2 |

- **Status:** `0x26`
- **Ant:** 안테나 번호
- **ReadRate:** 초당 태그 식별률
- **TotalCount:** 현재 인벤토리에서 감지된 총 태그 수

**2) 태그 데이터 응답 (Status ≠ 0x26)**
식별된 각 태그에 대한 정보를 데이터 패킷 형태로 반환합니다.

| Len | Adr | reCmd | Status | Ant | Num | Data Packet(s) | CRC-16 |
| :-- | :-- | :--- | :--- | :- | :-- | :--- | :--- |
| 가변 | 1 | 1 | 1 | 1 | 1 | 가변 | 2 |

- **Status:** `0x01`(성공), `0x02`(타임아웃) 등
- **Ant:** 안테나 번호
- **Num:** 응답에 포함된 데이터 패킷의 수
- **Data Packet(s):** 아래 "데이터 패킷 포맷" 참조

**데이터 패킷 포맷:**

| PacketParam | Len | Data | RSSI | Phase | Freq |
| :--- | :-- | :-- | :-- | :-- | :-- |
| 1 | 1 | 가변 | 1 | 4 | 3 |

- **PacketParam:** 패킷 타입(EPC 또는 읽은 데이터)과 시리얼 번호를 나타내는 플래그
- **Len:** `Data` 필드의 길이
- **Data:** EPC 번호 또는 `ReadMem`에서 요청한 데이터
- **RSSI:** 태그 신호 강도
- **Phase:** 위상 정보 (활성화 시)
- **Freq:** 주파수 정보 (활성화 시)
