"""Central configuration for the flood-EWS integration harness.

Everything environment-specific lives here so the test cases never
hardcode a serial number or a topic. Override via env vars in CI / on
the Pi without editing test code.
"""

import logging
import os
import sys
from dataclasses import dataclass

# Configure the global logging settings
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
    handlers=[
        logging.StreamHandler(sys.stdout),  # Output to standard console.
        logging.FileHandler("test.log"),  # Output to file.
    ],
)

# Thread credentials.
NETWORK_KEY = "00112233445566778899aabbccddeeff"
CHANNEL = "15"
PANID = "0x1234"
XPANID = "1111111122222222"


@dataclass(frozen=True)
class NodeCfg:
    """One physical fog/Raft node."""

    name: str  # logical name used in assertions, e.g. "fog-A"
    jlink_serial: int  # SEGGER J-Link serial for pylink
    eui64: str  # EUI-64 identity == raft_node_id
    device_eui: str  # hex string as emitted by the codec


# Cluster topology.
FOG_NODES = [
    NodeCfg(
        "Fog-A",
        jlink_serial=1050046046,
        eui64="f4ce360a4eac3643",
        device_eui="f4ce360a4eac3643",
    ),
    NodeCfg(
        "Fog-B",
        jlink_serial=1050088994,
        eui64="f4ce363a5c916092",
        device_eui="f4ce363a5c916092",
    ),
    NodeCfg(
        "Fog-C",
        jlink_serial=1050031651,
        eui64="f4ce3696247825d1",
        device_eui="f4ce3696247825d1",
    ),
]

# ChirpStack MQTT
MQTT_HOST = os.getenv("CHIRPSTACK_MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("CHIRPSTACK_MQTT_PORT", "1883"))
CHIRPSTACK_APP_ID = os.getenv("CHIRPSTACK_APP_ID", "1")
# Subscribe wide; filter in-process. Covers uplink + join events.
MQTT_UP_TOPIC = "application/+/device/+/event/up"
MQTT_JOIN_TOPIC = "application/+/device/+/event/join"

# ChirpStack API.
CHIRPSTACK_API = os.getenv("CHIRPSTACK_API", "http://127.0.0.1:8080")
CHIRPSTACK_API_TOKEN = os.getenv("CHIRPSTACK_API_TOKEN", "")

# Supabase
SUPABASE_URL = os.getenv("SUPABASE_URL", "")
SUPABASE_KEY = os.getenv("SUPABASE_KEY", "")
SUPABASE_TABLE = os.getenv("SUPABASE_TABLE", "telemetry_event")

# Path for ot-ctl
OT_CTL_PATH = [
    "/home/oliver/projects/ot-nrf528xx/openthread/build/posix/src/posix/ot-ctl"
]


# RTT log markers
class Marker:
    """Markers to look for over RTT."""

    BECAME_LEADER = "RAFT:BECAME_LEADER"
    BECAME_FOLLOWER = "RAFT:BECAME_FOLLOWER"
    COMMITTED = "RAFT:COMMITTED"  # + " idx=<n>"
    ENTER_EGRESS_WINDOW = "EGRESS:ENTER_WINDOW"  # emitted right before AT send
    AT_SEND_ISSUED = "EGRESS:AT_SEND"
    UPLINK_CONFIRMED = "EGRESS:UPLINK_CONFIRMED"
    MARK_EGRESSED = "EGRESS:MARK_EGRESSED"
    FAULT_HOOK_PAUSED = "FAULT:PAUSED_IN_WINDOW"  # one-shot delay hook active
