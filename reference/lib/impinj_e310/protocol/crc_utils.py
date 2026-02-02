"""
CRC calculation utilities for E310 protocol
Implements CRC16-CCITT algorithm
"""

def calculate_crc16(data: bytes) -> int:
    """
    Calculate CRC16-CCITT
    Args:
        data (bytes): Data to calculate CRC
    Returns:
        int: Calculated CRC16 value
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

def validate_crc(frame: bytes) -> bool:
    """
    Validate CRC of received frame
    Args:
        frame (bytes): Complete frame including CRC
    Returns:
        bool: CRC validation result
    """
    if len(frame) < 3:
        return False

    # Last 2 bytes are CRC (LSB, MSB)
    data = frame[:-2]
    received_crc = (frame[-1] << 8) | frame[-2]
    calculated_crc = calculate_crc16(data)

    return received_crc == calculated_crc
