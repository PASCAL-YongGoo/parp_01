"""
MicroPython UART communication interface for PARP_01 Board
Optimized for STM32H723ZG with UART4 (PD1=TX, PD0=RX)
"""

from machine import UART, Pin
import time
from ..utils.logger import Logger
from ..utils.config import Config

class UARTInterface:
    """UART communication interface for MicroPython"""

    def __init__(self, uart_id: int = None, baudrate: int = None, timeout: int = None):
        """
        Initialize UART interface
        Args:
            uart_id (int): UART ID (default: Config.UART_ID = 4)
            baudrate (int): Baudrate (default: Config.BAUDRATE = 115200)
            timeout (int): Timeout in ms (default: Config.UART_TIMEOUT = 1000)
        """
        self.uart_id = uart_id or Config.UART_ID
        self.baudrate = baudrate or Config.BAUDRATE
        self.timeout = timeout or Config.UART_TIMEOUT
        self.uart = None
        self.is_connected = False

        Logger.info(f"UARTInterface init: UART{self.uart_id}, {self.baudrate} baud")

    def open(self) -> bool:
        """
        Open UART port
        Returns:
            bool: Success status
        """
        try:
            # Close existing connection if any
            if self.uart:
                self.uart.deinit()
                time.sleep_ms(50)

            # Initialize UART4 (PD1=TX, PD0=RX for PARP_01 board)
            if self.uart_id == 4:
                self.uart = UART(self.uart_id, baudrate=self.baudrate,
                               tx=Pin('D1'), rx=Pin('D0'),
                               timeout=self.timeout)
            else:
                self.uart = UART(self.uart_id, baudrate=self.baudrate,
                               timeout=self.timeout)

            # Clear RX buffer
            if self.uart.any():
                self.uart.read()

            self.is_connected = True
            Logger.info(f"UART{self.uart_id} connected successfully")
            return True

        except Exception as e:
            Logger.error(f"UART connection failed: {e}")
            self.is_connected = False
            return False

    def close(self):
        """Close UART port"""
        if self.uart:
            self.uart.deinit()
            self.is_connected = False
            Logger.info("UART closed")

    def write(self, data: bytes) -> int:
        """
        Write data to UART
        Args:
            data (bytes): Data to send
        Returns:
            int: Number of bytes sent
        """
        if not self.is_connected or not self.uart:
            Logger.error("UART not connected")
            return 0

        try:
            sent = self.uart.write(data)
            Logger.debug(f"TX: {data.hex()} ({sent} bytes)")
            return sent
        except Exception as e:
            Logger.error(f"UART write error: {e}")
            return 0

    def read(self, size: int = None) -> bytes:
        """
        Read data from UART
        Args:
            size (int): Number of bytes to read (None = all available)
        Returns:
            bytes: Received data
        """
        if not self.is_connected or not self.uart:
            return b''

        try:
            if size is None:
                data = self.uart.read()
            else:
                data = self.uart.read(size)

            if data:
                Logger.debug(f"RX: {data.hex()} ({len(data)} bytes)")
                return data
            return b''
        except Exception as e:
            Logger.error(f"UART read error: {e}")
            return b''

    def available(self) -> int:
        """
        Get number of bytes available in RX buffer
        Returns:
            int: Number of available bytes
        """
        if not self.is_connected or not self.uart:
            return 0
        return self.uart.any()

    def flush(self):
        """Flush TX buffer (auto-flushed in MicroPython)"""
        pass
