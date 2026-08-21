"""ChirpStack downstream handler.

Handle messages sent to chirpstack and then passed downstream with the MQTT
broker.
"""

import asyncio
import base64
import json
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Any

import paho.mqtt.client as mqtt

from config import MQTT_HOST, MQTT_JOIN_TOPIC, MQTT_PORT, MQTT_UP_TOPIC


@dataclass
class Uplink:
    """The uplink sent over chirpstack."""

    ts: float
    dev_eui: str
    f_port: int
    f_cnt: int
    payload: bytes  # Raw output.
    decoded: dict  # JS Codec output.
    raw: dict = field(repr=False, default_factory=dict)


class ChirpStackHandler:
    """Handle MQTT messages sent from chirsptack when a new uplink is received."""

    def __init__(self) -> None:
        """Initialise ChirpStackHandler."""
        self._client = mqtt.Client()
        self._client.on_message = self._on_message
        self._uplinks: list[Uplink] = []
        self._joins: list[dict] = []
        self._lock = threading.Lock()
        self._connected = threading.Event()
        self._client.on_connect = lambda *_: self._connected.set()

    def start(self) -> None:
        """Attach to the MQTT broker and listen for messages over chirpstack.

        Raises:
            RuntimeError: Could not connect to MQTT.
        """
        self._client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
        self._client.subscribe([(MQTT_UP_TOPIC, 0), (MQTT_JOIN_TOPIC, 0)])
        self._client.loop_start()
        if not self._connected.wait(timeout=5):
            msg = "MQTT connect timed out"
            raise RuntimeError(msg)

    def _on_message(self, _c, _u, msg) -> None:  # noqa: ANN001
        """Called on a new message."""
        try:
            data = json.loads(msg.payload)
        except json.JSONDecodeError:
            return
        now = time.monotonic()
        if msg.topic.endswith("/event/up"):
            raw_b64 = data.get("data", "")
            payload = base64.b64decode(raw_b64) if raw_b64 else b""
            up = Uplink(
                ts=now,
                dev_eui=data.get("deviceInfo", {}).get("devEui", ""),
                f_port=data.get("fPort", -1),
                f_cnt=data.get("fCnt", -1),
                payload=payload,
                decoded=data.get("object", {}) or {},
                raw=data,
            )
            with self._lock:
                self._uplinks.append(up)
        elif msg.topic.endswith("/event/join"):
            with self._lock:
                self._joins.append(data)

    def mark(self) -> float:
        """Return a timestamp to filter subsequent queries against.

        Returns:
            float: The timestamp.
        """
        return time.monotonic()

    def uplinks_since(self, since: float) -> list[Uplink]:
        """The number of uplinks since time `since`.

        Args:
            since (float): The timestamp to check from.

        Returns:
            list[Uplink]: The uplinks.
        """
        with self._lock:
            return [u for u in self._uplinks if u.ts >= since]

    async def wait_for_uplink(
        self,
        since: float,
        timeout: float,
        predicate: Callable[[Any], bool] | None = None,
    ) -> Uplink:
        """Block until an uplink after `since` matching `predicate` arrives.

        Args:
            since (float): The time to check from.
            timeout (float): How long to wait for an uplink.
            predicate (Callable[[Any], bool] | None, optional): Predicate to filter on.
                                                                Defaults to None.

        Raises:
            TimeoutError: If no uplink was returned.

        Returns:
            Uplink: The uplink that was found.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for u in self.uplinks_since(since):
                if predicate is None or predicate(u):
                    return u
            await asyncio.sleep(0.05)
        msg = f"no matching uplink within {timeout}s"
        raise TimeoutError(msg)

    def count_uplinks(
        self, since: float, predicate: Callable[[Any], bool] | None = None
    ) -> int:
        """Count the number of uplinks since time `since` filter on `predicate`.

        Args:
            since (float): The time to check from.
            predicate (Callable[[Any], bool] | None, optional): Predicate to filter on.
                                                                Defaults to None.

        Returns:
            int: The number of uplinks.
        """
        return sum(
            1 for u in self.uplinks_since(since) if predicate is None or predicate(u)
        )

    def stop(self) -> None:
        """Stop listening to MQTT messages."""
        self._client.loop_stop()
        self._client.disconnect()
