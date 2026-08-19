"""Wire format for the /fault_edge CoAP payload.

struct SensorReadingWire {           offset
  uint64_t            m_eui;         0
  uint64_t            m_timestamp;   8
  uint16_t            m_water_level_mm; 16
  IEC61850_Validity   m_validity;    18   (enum class : uint8_t)
  IEC61850_DetailQual m_detail;      19   (enum class : uint8_t)
  uint8_t             m_seq;         20
  // 3 bytes tail padding -> size 24, align 8
};
struct FaultMessage {
  SensorReadingWire   m_bad_reading; 0
  bool                m_active;      24
  // 7 bytes tail padding -> size 32, align 8
};
"""

import struct
from enum import IntEnum


class Validity(IntEnum):
    """The IEC61850_Validity values."""

    GOOD = 0
    INVALID = 1
    QUESTIONABLE = 2


class DetailQual(IntEnum):
    """The IEC61850_DetailQual values."""

    NONE = 0x00
    OVERFLOW = 0x01
    OUT_OF_RANGE = 0x02
    FAILURE = 0x04
    OLD_DATA = 0x08
    OUTLIER = 0x10


# little-endian, standard sizes: Q eui | Q timestamp | H water_level | B
# validity | B detail | I seq | B active | B sign | 6x
# FaultMessage tail pad
FOG_FAULT_FMT = "<QQHBBIBB6x"
FOG_FAULT_SIZE = struct.calcsize(FOG_FAULT_FMT)
FOG_FAULT_EXPECTED_SIZE = 32
assert FOG_FAULT_SIZE == FOG_FAULT_EXPECTED_SIZE, (
    f"FaultMessage wire size drifted: {FOG_FAULT_SIZE} != {FOG_FAULT_EXPECTED_SIZE}"
)


def pack_fault(  # noqa: PLR0913
    *,
    eui: int,
    timestamp: int,
    water_level_mm: int,
    validity: Validity,
    detail: DetailQual,
    seq: int,
    active: bool,
    sign: bool,
) -> bytes:
    """Serialise a fault message.

    Args:
        eui (int): The node EUI (ignored in FW)
        timestamp (int): The timestamp
        water_level_mm (int): The water level in millilitres
        validity (Validity): Validity flag
        detail (DetailQual): Detail flags
        seq (int): Seq (Ignored in FW currently)
        active (bool): Whether the fault is active.
        sign (bool): Whether the fault should tamper with the message signature.

    Returns:
        bytes: The packed data.
    """
    return struct.pack(
        FOG_FAULT_FMT,
        eui & 0xFFFFFFFFFFFFFFFF,
        timestamp & 0xFFFFFFFFFFFFFFFF,
        water_level_mm & 0xFFFF,
        int(validity),
        int(detail),
        seq & 0xFFFFFFFF,
        1 if active else 0,
        1 if sign else 0
    )


def pack_clear() -> bytes:
    """A fault message with m_active = false resets a node to real readings."""
    return pack_fault(
        eui=0,
        timestamp=0,
        water_level_mm=0,
        validity=Validity.GOOD,
        detail=DetailQual.NONE,
        seq=0,
        active=False,
        sign=False
    )
