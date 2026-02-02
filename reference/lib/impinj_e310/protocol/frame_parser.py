"""
Frame parser for E310 protocol responses
Parses response frames and extracts data
"""

import struct
from .crc_utils import validate_crc

class FrameParser:
    """Frame parser for E310 responses"""

    @staticmethod
    def parse_response(data: bytes, verify_crc: bool = True) -> dict:
        """
        Parse response frame
        Args:
            data (bytes): Received response frame
            verify_crc (bool): Perform CRC verification
        Returns:
            dict: Parsed result
        """
        if len(data) < 5:
            return {
                "success": False,
                "error": "Frame too short",
                "frame_data": data.hex() if data else ""
            }

        # CRC verification
        if verify_crc and not validate_crc(data):
            return {
                "success": False,
                "error": "CRC verification failed",
                "frame_data": data.hex()
            }

        length = data[0]
        addr = data[1]
        recmd = data[2]
        status = data[3]
        payload = data[4:-2]  # Exclude CRC

        result = {
            "length": length,
            "addr": addr,
            "recmd": recmd,
            "status": status,
            "payload": payload,
            "success": status == 0x00
        }

        if status != 0x00:
            result["error"] = FrameParser._get_error_description(status)

        return result

    @staticmethod
    def parse_inventory_response(data: bytes) -> dict:
        """
        Parse inventory response
        Args:
            data (bytes): Inventory response frame
        Returns:
            dict: Result with EPC list and RSSI
        """
        base_result = FrameParser.parse_response(data)

        # Status 0x03 = more data in following frames (still valid)
        if (base_result["status"] == 0x00 or base_result["status"] == 0x03) and base_result.get("payload"):
            epc_list, rssi_list = FrameParser._parse_epc_list(base_result["payload"])
            base_result["epc_list"] = epc_list
            base_result["rssi_list"] = rssi_list
            base_result["tag_count"] = len(epc_list)
            base_result["success"] = True
        else:
            base_result["epc_list"] = []
            base_result["rssi_list"] = []
            base_result["tag_count"] = 0

        return base_result

    @staticmethod
    def parse_read_response(data: bytes) -> dict:
        """
        Parse read response
        Args:
            data (bytes): Read response frame
        Returns:
            dict: Result with read data
        """
        base_result = FrameParser.parse_response(data)

        if base_result["success"] and base_result.get("payload"):
            base_result["read_data"] = base_result["payload"]
        else:
            base_result["read_data"] = b''

        return base_result

    @staticmethod
    def _parse_epc_list(payload: bytes) -> tuple:
        """
        Parse EPC data list from inventory response

        Format: Ant(1) + Num(1) + [DataLen(1) + EPC(n) + RSSI(1)] * Num

        Args:
            payload (bytes): Payload with EPC data
        Returns:
            tuple: (epc_list, rssi_list)

        Example:
            payload: 01 01 10 850470002139434b3257303200700105 55
                     ^Ant ^Num ^Len ^^^^^^^^16 bytes EPC^^^^^^^^ ^RSSI
        """
        epc_list = []
        rssi_list = []

        if len(payload) < 2:
            return epc_list, rssi_list

        ant = payload[0]  # Antenna number
        num = payload[1]  # Number of tags
        offset = 2

        for i in range(num):
            if offset >= len(payload):
                break

            # Read EPC data length
            data_len = payload[offset]
            offset += 1

            if offset + data_len + 1 > len(payload):
                break

            # Extract EPC (data_len bytes)
            epc = payload[offset:offset + data_len]
            offset += data_len

            # Extract RSSI (1 byte)
            rssi = payload[offset]
            offset += 1

            epc_list.append(epc)
            rssi_list.append(rssi)

        return epc_list, rssi_list

    @staticmethod
    def _get_error_description(status_code: int) -> str:
        """
        Get error description from status code
        Args:
            status_code (int): Status code
        Returns:
            str: Error description
        """
        error_codes = {
            0x01: "Command execution failed",
            0x02: "Inventory failed (no tags)",
            0x03: "More data in following frames",
            0x04: "Kill failed",
            0x05: "Frame error",
            0x06: "CRC error",
            0x07: "Memory out of range",
            0x08: "Access password error",
            0x09: "Parameter error",
            0x0A: "Memory locked",
            0x0B: "Insufficient power",
            0x0C: "Timeout"
        }
        return error_codes.get(status_code, f"Unknown error (0x{status_code:02x})")
