"""
Frame builder for E310 protocol commands
Builds command frames with proper CRC
"""

import struct
from .crc_utils import calculate_crc16

class FrameBuilder:
    """Frame builder for E310 commands"""

    @staticmethod
    def build_inventory_frame(addr: int) -> bytes:
        """
        Build inventory command frame
        Args:
            addr (int): Reader address (0x00-0xFE)
        Returns:
            bytes: Inventory command frame
        """
        length = 5  # Len(1) + Adr(1) + Cmd(1) + CRC16(2)
        cmd = 0x01
        frame = bytearray([length, addr, cmd])
        crc = calculate_crc16(frame)
        frame += struct.pack('<H', crc)  # Little-endian CRC
        return bytes(frame)

    @staticmethod
    def build_read_frame(addr: int, mem_bank: int, word_ptr: int,
                        word_cnt: int, password: int = 0x00000000) -> bytes:
        """
        Build read command frame
        Args:
            addr (int): Reader address
            mem_bank (int): Memory bank (0x00-0x03)
            word_ptr (int): Starting word address
            word_cnt (int): Number of words to read
            password (int): Access password
        Returns:
            bytes: Read command frame
        """
        length = 13  # Len(1) + Adr(1) + Cmd(1) + Mem(1) + WordPtr(2) + WordCnt(1) + Pwd(4) + CRC16(2)
        cmd = 0x02
        frame = bytearray([length, addr, cmd, mem_bank])
        frame += struct.pack('>H', word_ptr)  # Big-endian word pointer
        frame += struct.pack('B', word_cnt)
        frame += struct.pack('>I', password)  # Big-endian password
        crc = calculate_crc16(frame)
        frame += struct.pack('<H', crc)  # Little-endian CRC
        return bytes(frame)

    @staticmethod
    def build_write_frame(addr: int, mem_bank: int, word_ptr: int,
                         data: bytes, password: int = 0x00000000) -> bytes:
        """
        Build write command frame
        Args:
            addr (int): Reader address
            mem_bank (int): Memory bank
            word_ptr (int): Starting word address
            data (bytes): Data to write (must be word-aligned)
            password (int): Access password
        Returns:
            bytes: Write command frame
        """
        word_cnt = len(data) // 2  # Word count (2 bytes = 1 word)
        length = 8 + len(data) + 4 + 2  # Fields + data + password + CRC
        cmd = 0x03

        frame = bytearray([length, addr, cmd, mem_bank])
        frame += struct.pack('>H', word_ptr)  # Big-endian word pointer
        frame += struct.pack('B', word_cnt)
        frame += data
        frame += struct.pack('>I', password)  # Big-endian password
        crc = calculate_crc16(frame)
        frame += struct.pack('<H', crc)  # Little-endian CRC
        return bytes(frame)

    @staticmethod
    def build_kill_frame(addr: int, kill_password: int) -> bytes:
        """
        Build kill command frame
        Args:
            addr (int): Reader address
            kill_password (int): Kill password
        Returns:
            bytes: Kill command frame
        """
        length = 8  # Len, Adr, Cmd, KillPwd(4), CRC16(2)
        cmd = 0x05
        frame = bytearray([length, addr, cmd])
        frame += struct.pack('>I', kill_password)  # Big-endian password
        crc = calculate_crc16(frame)
        frame += struct.pack('<H', crc)  # Little-endian CRC
        return bytes(frame)
