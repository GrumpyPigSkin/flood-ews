"""Per-node J-Link control and RTT log capture.

During testing each Fog node should be connected over J-Link to the PI running
the tests.

Tests can then access log data and issue commands to the J-Links on each node to
track state and put the nodes into fault conditions.
"""

import queue
import threading
import time
from contextlib import contextmanager, suppress
from pathlib import Path

import pylink

from config import NodeCfg

NRF5340_APP = "nRF5340_xxAA_APP"
RTT_POLL_INTERVAL = 0.01


class RTTCapture:
    """Background thread draining RTT messages into a timestamped line buffer."""

    def __init__(self, jlink: pylink.JLink, name: str) -> None:
        """Initialise RTTCapture.

        Args:
            jlink (pylink.JLink): The JLink node to capture.
            name (str): Description of the node "fog-a" etc...
        """
        self._jlink = jlink
        self._name = name
        self._lines: queue.Queue[tuple[float, str]] = queue.Queue()
        self._history: list[tuple[float, str]] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._partial = ""

    def start(self) -> None:
        """Start the RTT Capture."""
        self._jlink.rtt_start()
        # Give the control block a moment to be located.
        time.sleep(0.3)
        self._thread.start()

    def _run(self) -> None:
        """The main thread function.

        Drain the captured rtt into lines and history.
        """
        while not self._stop.is_set():
            try:
                data = self._jlink.rtt_read(0, 1024)
            except pylink.errors.JLinkRTTException:
                time.sleep(RTT_POLL_INTERVAL)
                continue
            if data:
                text = self._partial + bytes(data).decode("utf-8", "replace")
                *complete, self._partial = text.split("\n")
                now = time.monotonic()
                for line in complete:
                    entry = (now, line.rstrip("\r"))
                    self._history.append(entry)
                    self._lines.put(entry)
            else:
                time.sleep(RTT_POLL_INTERVAL)

    def wait_for(self, marker: str, timeout: float) -> tuple[float, str]:
        """Block until we see the string we want in a line over RTT.

        Args:
            marker (str): The string to look for.
            timeout (float): Timeout to wait for the string.

        Raises:
            TimeoutError: Raised if we timeout without seeing the string.

        Returns:
            tuple[float, str]: The timestamp and the string.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                ts, line = self._lines.get(timeout=deadline - time.monotonic())
            except queue.Empty:
                break
            if marker in line:
                return ts, line
        msg = f"[{self._name}] marker {marker!r} not seen within {timeout}s"
        raise TimeoutError(msg)

    def drain_new(self) -> None:
        """Discard buffered-but-unread lines so the next wait_for starts clean."""
        while not self._lines.empty():
            try:
                self._lines.get_nowait()
            except queue.Empty:
                break

    def history(self) -> list[tuple[float, str]]:
        """Get the full history for this node."""
        return list(self._history)

    def stop(self) -> None:
        """Stop the background capture thread."""
        self._stop.set()
        self._thread.join(timeout=2)

    def dump_to_file(self, path: str) -> None:
        """Write full captured history to a file,."""
        with Path.open(path, "w") as f:
            f.writelines(f"[{ts:.3f}] {line}\n" for ts, line in self._history)


# The libs for jlink hold some global state that means we cannot run multiple
# instances at once, to get around this, I copied the lib 3 times and we need to
# access these libs directly for testing.
_LIB_PATHS = [
    "/opt/SEGGER/JLink_V794e/libjlinkarm_node0.so",
    "/opt/SEGGER/JLink_V794e/libjlinkarm_node1.so",
    "/opt/SEGGER/JLink_V794e/libjlinkarm_node2.so",
]


class JLinkNode:
    """Owns a single J-Link connection to one device."""

    def __init__(self, cfg: NodeCfg, index: int, device: str = NRF5340_APP) -> None:
        """Initialise JLinkNode.

        Args:
            cfg (NodeCfg): The node config, containing the serial number to look for.
            index (int): The index of each new node to access the LIB_PATH.
            device (str, optional): The device. Defaults to NRF5340_APP.
        """
        self.cfg = cfg
        self._device = device
        lib = pylink.Library(dllpath=_LIB_PATHS[index])
        self._jlink = pylink.JLink(lib=lib)
        self.rtt: RTTCapture | None = None

    def open(self) -> None:
        """Open the connection to J-Link."""
        self._jlink.open(serial_no=self.cfg.jlink_serial)
        self._jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
        self._jlink.connect(self._device)
        self.rtt = RTTCapture(self._jlink, self.cfg.name)
        self.rtt.start()

    def resume(self) -> None:
        """Bring a halted node back so it rejoins the cluster."""
        if self._jlink.halted():
            self._jlink.restart()

    def reset_and_hold(self) -> None:
        """Reset the target and hold it.

        Keeping it in reset until resume() explicitly releases it.
        """
        self._jlink.reset(halt=True)

    def close(self) -> None:
        """Close the connection."""
        if self.rtt:
            self.rtt.stop()
        with suppress(Exception):
            self._jlink.close()

    @contextmanager
    def session(self) -> any:
        """The context session manager."""
        self.open()
        try:
            yield self
        finally:
            self.close()
