"""
Configuration for Impinj E310 RFID Reader
Optimized for PARP_01 Board (STM32H723ZG)
"""

class Config:
    """E310 설정 클래스"""

    # UART 설정 (PARP_01 보드 기준)
    UART_ID = 4              # UART4 사용 (PD1=TX, PD0=RX)
    BAUDRATE = 115200        # E310 기본 보드레이트
    UART_TIMEOUT = 1000      # UART 타임아웃 (ms)

    # E310 Reader 설정
    READER_ADDRESS = 0x00    # Reader 주소 (기본값)
    MAX_FRAME_LENGTH = 255   # 최대 프레임 길이

    # 명령 타임아웃 설정
    COMMAND_TIMEOUT = 2000   # 명령 응답 대기 시간 (ms)
    INVENTORY_TIMEOUT = 3000 # 인벤토리 타임아웃 (ms)
    READ_TIMEOUT = 2000      # 읽기 타임아웃 (ms)
    WRITE_TIMEOUT = 2000     # 쓰기 타임아웃 (ms)

    # 재시도 설정
    MAX_RETRIES = 3          # 최대 재시도 횟수
    RETRY_DELAY = 100        # 재시도 간 대기 시간 (ms)

    # 태그 버퍼
    MAX_TAG_BUFFER = 100     # 최대 태그 버퍼 크기

    # 메모리 뱅크 정의
    MEM_BANK_RESERVED = 0x00
    MEM_BANK_EPC = 0x01
    MEM_BANK_TID = 0x02
    MEM_BANK_USER = 0x03
