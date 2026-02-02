"""
Impinj E310 MicroPython Library for PARP_01 Board

PARP_01 (STM32H723ZG) port for E310 RFID module control
"""

from .impinj_e310 import ImpinjE310, Tag

__version__ = "1.0.0"
__all__ = ['ImpinjE310', 'Tag']
