### 0x1b QT Inventory Command

이 명령어는 Public Mirroring 기능이 활성화된 Impinj Monza 4QT 태그의 Private EPC 번호를 조회(인벤토리)하기 위해 특별히 사용됩니다. 일반 인벤토리(`0x01`)와 유사한 방식으로 동작하지만, Monza 4QT 태그의 특정 기능을 대상으로 합니다.

**중요:** 이 명령어는 **Impinj Monza 4QT** 태그의 QT 기능을 사용할 때 유효합니다.

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
| `Cmd` | 1 | 명령어 코드. `0x1b`로 고정. |
| `Data[]`| 가변 | QT 인벤토리 파라미터. `0x01` 명령어와 동일한 파라미터(`QValue`, `Session`, `Target`, `Ant`, `Scantime` 등)를 사용합니다. |
| `CRC-16`| 2 | `Len`부터 `Data[]`까지의 CRC-16 체크섬. |

---

#### 2. 응답 프레임 (E310 → PC)

응답 프레임은 일반 인벤토리(`0x01`)와 동일한 형식을 따릅니다. 인벤토리 후 통계 정보를 반환하거나, 식별된 태그 정보를 반환할 수 있습니다.

**1) 통계 데이터 응답 (Status = 0x26)**

| Len | Adr | reCmd | Status | Ant | ReadRate | TotalCount | CRC-16 |
| :-- | :-- | :--- | :--- | :- | :--- | :--- | :--- |
| 가변 | 1 | 1 | 1 | 1 | 2 | 4 | 2 |

- **Status:** `0x26`
- **Ant:** 안테나 번호
- **ReadRate:** 초당 태그 식별률
- **TotalCount:** 현재 인벤토리에서 감지된 총 태그 수

**2) 태그 데이터 응답 (Status ≠ 0x26)**

| Len | Adr | reCmd | Status | Ant | Num | EPC ID(s) | CRC-16 |
| :-- | :-- | :--- | :--- | :- | :-- | :--- | :--- |
| 가변 | 1 | 1 | 1 | 1 | 1 | 가변 | 2 |

- **Status:** `0x01`(성공), `0x02`(타임아웃) 등
- **Ant:** 안테나 번호
- **Num:** 응답에 포함된 태그의 수
- **EPC ID(s):** 식별된 태그의 EPC 정보 (`EPC Length` + `EPC Number` + `RSSI`)
