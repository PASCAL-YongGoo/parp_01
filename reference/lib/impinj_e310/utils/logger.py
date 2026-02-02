"""
Simple logger for MicroPython
Lightweight logging with minimal memory footprint
"""

class Logger:
    """간단한 로거 클래스"""

    # 로그 레벨
    DEBUG = 0
    INFO = 1
    WARNING = 2
    ERROR = 3
    NONE = 4

    # 현재 로그 레벨 (기본값: INFO)
    level = INFO

    @staticmethod
    def debug(msg):
        """디버그 메시지 출력"""
        if Logger.level <= Logger.DEBUG:
            print(f"[DEBUG] {msg}")

    @staticmethod
    def info(msg):
        """정보 메시지 출력"""
        if Logger.level <= Logger.INFO:
            print(f"[INFO] {msg}")

    @staticmethod
    def warning(msg):
        """경고 메시지 출력"""
        if Logger.level <= Logger.WARNING:
            print(f"[WARN] {msg}")

    @staticmethod
    def error(msg):
        """에러 메시지 출력"""
        if Logger.level <= Logger.ERROR:
            print(f"[ERROR] {msg}")

    @staticmethod
    def set_level(level):
        """로그 레벨 설정"""
        Logger.level = level
