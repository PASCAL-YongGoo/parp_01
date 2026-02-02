"""
Impinj E310 MicroPython control class
Optimized for PARP_01 Board (STM32H723ZG)
"""

import time
from .communication import UARTInterface
from .protocol import FrameBuilder, FrameParser
from .utils import Logger, Config

class Tag:
    """Tag information class"""

    def __init__(self, epc: bytes, rssi: int = None, timestamp: int = None):
        """
        Initialize tag information
        Args:
            epc (bytes): EPC data
            rssi (int): Signal strength (optional)
            timestamp (int): Detection time in ms (optional)
        """
        self.epc = epc
        self.rssi = rssi
        self.timestamp = timestamp or time.ticks_ms()

    def __str__(self) -> str:
        hex_epc = ''.join(['{:02x}'.format(b) for b in self.epc])
        return f"Tag(EPC: {hex_epc}, RSSI: {self.rssi})"

    def __repr__(self) -> str:
        return self.__str__()


class ImpinjE310:
    """Impinj E310 MicroPython control class"""

    def __init__(self, uart_id: int = None, baudrate: int = None,
                 reader_addr: int = None):
        """
        Initialize E310 control class
        Args:
            uart_id (int): UART ID (default: Config.UART_ID = 4)
            baudrate (int): Baudrate (default: Config.BAUDRATE = 115200)
            reader_addr (int): Reader address (default: Config.READER_ADDRESS = 0x00)
        """
        self.reader_addr = reader_addr or Config.READER_ADDRESS

        # Initialize UART interface
        self.interface = UARTInterface(uart_id, baudrate)

        # Statistics
        self.command_count = 0
        self.error_count = 0

        Logger.info(f"ImpinjE310 initialized: UART{uart_id or Config.UART_ID}, Reader Addr: 0x{self.reader_addr:02X}")

    def connect(self) -> bool:
        """
        Connect to E310
        Returns:
            bool: Connection success status
        """
        Logger.info("Connecting to E310...")
        if self.interface.open():
            Logger.info("E310 connected successfully")
            return True
        else:
            Logger.error("E310 connection failed")
            return False

    def disconnect(self):
        """Disconnect from E310"""
        self.interface.close()
        Logger.info("E310 disconnected")

    def _send_command(self, frame: bytes, timeout_ms: int = None) -> bytes:
        """
        Send command and receive response
        Args:
            frame (bytes): Frame to send
            timeout_ms (int): Response timeout in ms
        Returns:
            bytes: Received response frame
        """
        timeout_ms = timeout_ms or Config.COMMAND_TIMEOUT
        self.command_count += 1

        # Send command
        if self.interface.write(frame) == 0:
            Logger.error("Command send failed")
            self.error_count += 1
            return b''

        # Wait for response
        start_time = time.ticks_ms()
        response = b''

        while True:
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed > timeout_ms:
                Logger.warning("Response timeout")
                self.error_count += 1
                return b''

            data = self.interface.read(1)
            if data:
                response += data
                # Check response length (first byte is length)
                if len(response) >= 1:
                    expected_length = response[0]
                    if len(response) >= expected_length:
                        Logger.debug(f"Response received: {response.hex()}")
                        return response
            else:
                time.sleep_ms(1)

    def inventory(self, timeout_ms: int = None) -> list:
        """
        Perform tag inventory
        Args:
            timeout_ms (int): Response timeout in ms
        Returns:
            list: List of discovered tags
        """
        Logger.info("Executing inventory command")

        # Build inventory command frame
        frame = FrameBuilder.build_inventory_frame(self.reader_addr)

        # Send command and receive response
        response = self._send_command(frame, timeout_ms or Config.INVENTORY_TIMEOUT)

        if not response:
            Logger.error("No inventory response")
            return []

        # Parse response
        result = FrameParser.parse_inventory_response(response)

        if result["success"]:
            tags = [Tag(epc) for epc in result["epc_list"]]
            Logger.info(f"Inventory success: {len(tags)} tags found")
            return tags
        else:
            Logger.error(f"Inventory failed: {result.get('error', 'Unknown')}")
            return []

    def read_tag(self, mem_bank: int, word_ptr: int, word_cnt: int,
                 password: int = 0x00000000, timeout_ms: int = None) -> bytes:
        """
        Read tag memory
        Args:
            mem_bank (int): Memory bank (0x00-0x03)
            word_ptr (int): Starting word address
            word_cnt (int): Number of words to read
            password (int): Access password
            timeout_ms (int): Response timeout in ms
        Returns:
            bytes: Read data
        """
        Logger.info(f"Executing read command: Bank=0x{mem_bank:02X}, Ptr={word_ptr}, Cnt={word_cnt}")

        # Build read command frame
        frame = FrameBuilder.build_read_frame(
            self.reader_addr, mem_bank, word_ptr, word_cnt, password
        )

        # Send command and receive response
        response = self._send_command(frame, timeout_ms or Config.READ_TIMEOUT)

        if not response:
            Logger.error("No read response")
            return b''

        # Parse response
        result = FrameParser.parse_read_response(response)

        if result["success"]:
            Logger.info(f"Read success: {len(result['read_data'])} bytes read")
            return result["read_data"]
        else:
            Logger.error(f"Read failed: {result.get('error', 'Unknown')}")
            return b''

    def write_tag(self, mem_bank: int, word_ptr: int, data: bytes,
                  password: int = 0x00000000, timeout_ms: int = None) -> bool:
        """
        Write tag memory
        Args:
            mem_bank (int): Memory bank
            word_ptr (int): Starting word address
            data (bytes): Data to write (must be word-aligned, multiple of 2 bytes)
            password (int): Access password
            timeout_ms (int): Response timeout in ms
        Returns:
            bool: Write success status
        """
        Logger.info(f"Executing write command: Bank=0x{mem_bank:02X}, Ptr={word_ptr}, Data={len(data)} bytes")

        # Validate data length (must be word-aligned)
        if len(data) % 2 != 0:
            Logger.error("Write data must be word-aligned (multiple of 2 bytes)")
            return False

        # Build write command frame
        frame = FrameBuilder.build_write_frame(
            self.reader_addr, mem_bank, word_ptr, data, password
        )

        # Send command and receive response
        response = self._send_command(frame, timeout_ms or Config.WRITE_TIMEOUT)

        if not response:
            Logger.error("No write response")
            return False

        # Parse response
        result = FrameParser.parse_response(response)

        if result["success"]:
            Logger.info("Write success")
            return True
        else:
            Logger.error(f"Write failed: {result.get('error', 'Unknown')}")
            return False

    def kill_tag(self, kill_password: int, timeout_ms: int = None) -> bool:
        """
        Kill tag (permanently disable)
        Args:
            kill_password (int): Kill password
            timeout_ms (int): Response timeout in ms
        Returns:
            bool: Kill success status
        """
        Logger.info("Executing kill command")

        # Build kill command frame
        frame = FrameBuilder.build_kill_frame(self.reader_addr, kill_password)

        # Send command and receive response
        response = self._send_command(frame, timeout_ms or Config.COMMAND_TIMEOUT)

        if not response:
            Logger.error("No kill response")
            return False

        # Parse response
        result = FrameParser.parse_response(response)

        if result["success"]:
            Logger.info("Kill success")
            return True
        else:
            Logger.error(f"Kill failed: {result.get('error', 'Unknown')}")
            return False

    def get_statistics(self) -> dict:
        """
        Get communication statistics
        Returns:
            dict: Statistics information
        """
        return {
            'command_count': self.command_count,
            'error_count': self.error_count,
            'error_rate': self.error_count / self.command_count if self.command_count > 0 else 0
        }
