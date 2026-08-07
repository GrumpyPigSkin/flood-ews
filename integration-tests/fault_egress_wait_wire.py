"""Wire format for the /fog_fault CoAP payload.

struct FaultMessage {
  std::uint32_t m_sleep_time_ms;
  bool m_pause_egress;
  // 3 bytes padding.
};
"""

import struct

# little-endian, standard sizes: I time_sleep_ms | B active | 3x FaultMessage tail pad
FOG_FAULT_FMT = "<IB3x"
FOG_FAULT_SIZE = struct.calcsize(FOG_FAULT_FMT)
EXPECTED_WIRE_SIZE = 8
assert FOG_FAULT_SIZE == EXPECTED_WIRE_SIZE, (
    f"FaultMessage wire size drifted:{FOG_FAULT_SIZE} != {EXPECTED_WIRE_SIZE}"
)


def pack_fog_fault(
    *,
    time_sleep_ms: int,
    active: bool,
) -> bytes:
    """Serialise the fog fault into the FOG_FAULT_FMT.

    Args:
        time_sleep_ms (int): The time to sleep in milliseconds.
        active (bool): Is the fault active.

    Returns:
        bytes: The packed struct.
    """
    return struct.pack(
        FOG_FAULT_FMT,
        time_sleep_ms,
        1 if active else 0,
    )


def pack_fog_clear() -> bytes:
    """A fault message with m_active = False resets a node to real readings."""
    return pack_fog_fault(
        time_sleep_ms=0,
        active=False,
    )
