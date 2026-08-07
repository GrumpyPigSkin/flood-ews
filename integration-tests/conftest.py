"""Test fixtures and helpers used throughout testing."""

import ipaddress
import queue
import threading
import time
from collections.abc import Callable
from typing import Any

import pytest

from chirpstack_handler import ChirpStackHandler
from config import FOG_NODES, OT_CTL_PATH, NodeCfg
from jlink_node import JLinkNode
from ot_ctl import OtCtl


def wait_for_any_uplink(
    chirpstack: ChirpStackHandler,
    mark: float,
    timeout: float,
    poll_interval: float = 0.5,
) -> any:
    """Wait for any uplink from any node within timeout.

    Args:
        chirpstack (_type_): The Chirpstack MQTT instance to monitor.
        mark (_type_): The time in which to check after.
        timeout (float): The timeout for a uplink.
        poll_interval (float, optional): The poll rate to check. Defaults to 0.5.

    Raises:
        TimeoutError: No result within timeout.

    Returns:
        any: The uplinks received.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        uplinks = chirpstack.uplinks_since(mark)
        if uplinks:
            return uplinks
        time.sleep(poll_interval)
    msg = f"no uplink received within {timeout}s"
    raise TimeoutError(msg)


def wait_for_first_marker(
    nodes: list[JLinkNode], marker: str, timeout: float
) -> JLinkNode:
    """Fan out one watcher thread per node and return whichever sees `marker` first.

    Args:
        nodes (list[JLinkNode]): The list of JLink nodes to watch for.
        marker (str): The marker to look for.
        timeout (float): The timeout in seconds.

    Raises:
        TimeoutError: No marker was found in the given time.

    Returns:
        JLinkNode: The JLink node that saw the marker.
    """
    result_q: queue.Queue[JLinkNode] = queue.Queue()
    deadline = time.monotonic() + timeout

    # Helper lambda.
    def _watch(node: JLinkNode) -> None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        try:
            node.rtt.wait_for(marker, remaining)
        except TimeoutError:
            return
        result_q.put(node)

    threads = [threading.Thread(target=_watch, args=(n,), daemon=True) for n in nodes]
    for t in threads:
        t.start()

    try:
        return result_q.get(timeout=timeout)
    except queue.Empty:
        msg = (
            f"marker {marker!r} not seen on any of "
            f"{[n.cfg.name for n in nodes]} within {timeout}s"
        )
        raise TimeoutError(msg) from None


def eui64_to_decimal(hex_eui: str) -> int:
    """Convert the hex_eui to a int.

    Args:
        hex_eui (str): The string to convert.

    Raises:
        ValueError: Conversion failed.

    Returns:
        int: The converted value.
    """
    cleaned = hex_eui.replace(":", "").replace("-", "").strip()
    expected_len = 16
    if len(cleaned) != expected_len:
        msg = (
            f"expected 16 hex chars (8 bytes) for EUI64, got {len(cleaned)} "
            f"from {hex_eui!r}"
        )
        raise ValueError(msg)
    return int(cleaned, 16)


RAFT_SERVICE_ENTERPRISE_NUMBER = 12345


def parse_netdata_services(raw: str) -> list[dict]:
    """Parse the `Services:` section of `ot-ctl netdata show` output.

    Each line looks like:

    12345 d12578249636cef4 fddead00beef00002f6a9c9b86811583 s 6c00 0

    i.e. <enterprise_number> <service_data_hex> <server_data_hex> s
    <rloc16_hex> <internal_sid>.
    """
    services = []
    in_services = False
    for line in raw.splitlines():
        stripped = line.strip()
        if stripped == "Services:":
            in_services = True
            continue
        if stripped.endswith(":"):
            in_services = False
            continue
        if not in_services or not stripped:
            continue
        parts = stripped.split()
        expected_parts = 5
        if len(parts) < expected_parts:
            continue
        enterprise_number, service_data_hex, server_data_hex = (
            parts[0],
            parts[1],
            parts[2],
        )
        try:
            services.append(
                {
                    "enterprise_number": int(enterprise_number),
                    "service_data": bytes.fromhex(service_data_hex),
                    "server_data": bytes.fromhex(server_data_hex),
                }
            )
        except ValueError:
            continue
    return services


def raft_peer_eui_hex(service_data: bytes) -> str:
    """Reverse the peer eui from net data to get the byte order shown in the uplink.

    Args:
        service_data (bytes): The data to reverse.

    Returns:
        str: The reversed data.
    """
    return service_data[::-1].hex()


def leader_ipv6_address(otctl: OtCtl, leader_cfg: NodeCfg) -> str:
    """Resolve a fog node's ML-EID via the Raft peer-discovery service it publishes.

    Args:
        otctl (OtCtl): The ot-ctl instance for netdata
        leader_cfg (_type_): The leader config.

    Raises:
        RuntimeError: If no service could be found for the leader.

    Returns:
        str: The IP address.
    """
    raw = otctl.run_command("netdata", "show")
    target = leader_cfg.device_eui.lower()
    expected_sd_len = 8
    for svc in parse_netdata_services(raw):
        if svc["enterprise_number"] != RAFT_SERVICE_ENTERPRISE_NUMBER:
            continue
        if len(svc["service_data"]) != expected_sd_len:
            continue
        if raft_peer_eui_hex(svc["service_data"]) == target:
            return str(ipaddress.IPv6Address(svc["server_data"]))
    msg = (
        f"could not find a Raft peer service matching device_eui="
        f"{leader_cfg.device_eui} in netdata show output:\n{raw}"
    )
    raise RuntimeError(msg)


def same_uplink(a: dict, b: dict, ignore_keys: tuple = ("node_id", "seq_id")) -> bool:
    """Check that two uplinks are the same.

    Args:
        a (dict): One uplink.
        b (dict): The other uplink
        ignore_keys (tuple, optional): Keys to ignore. Defaults to ("node_id, seq_id",).

    Returns:
        bool: True if the same.
    """
    fa = {k: v for k, v in a.items() if k not in ignore_keys}
    fb = {k: v for k, v in b.items() if k not in ignore_keys}
    return fa == fb


def wait_for_all_good(
    chirpstack: ChirpStackHandler,
    since: float,
    timeout: float,
    poll_interval: float = 1.0,
) -> None:
    """Watch every sensor entry across all uplinks in the window.

    Returns early as soon as at least one uplink has arrived and ALL entries
    seen so far are clean (GOOD and not outlier). Keeps waiting/polling up to
    `timeout` if bad entries exist or if no uplinks have arrived yet.

    Args:
        chirpstack (_type_): The chirpstack instance.
        since (float): Time to check from.
        timeout (float): Timeout to wait for all good.
        poll_interval (float, optional): Time to poll for new uplink.

    Raises:
        AssertionError: If any are remaining bad.
        TimeoutError: If we didn't see any more uplinks in timeout.
    """
    deadline = time.monotonic() + timeout
    last_bad_entries = []
    while time.monotonic() < deadline:
        uplinks = chirpstack.uplinks_since(since)
        if uplinks:
            current_bad = []
            for u in uplinks:
                current_bad.extend(
                    [
                        e
                        for e in u.decoded.get("entries", [])
                        if e.get("outlier") or e.get("validity") != "GOOD"
                    ]
                )
            # If we saw uplinks and NONE of them are bad -> success!
            if not current_bad:
                return
            last_bad_entries = current_bad
        time.sleep(poll_interval)
    if last_bad_entries:
        msg = f"baseline not clean after {timeout}s, bad entries: {last_bad_entries}"
        raise AssertionError(msg)
    msg_0 = f"no uplinks observed within {timeout}s to confirm baseline"
    raise TimeoutError(msg_0)


def wait_for_entry_state_by_predicate(
    chirpstack: ChirpStackHandler,
    since: float,
    predicate: Callable[[Any], bool],
    timeout: float,
    poll_interval: float = 1.0,
) -> dict:
    """Wait for an entry to reach the state required in predicate.

    Args:
        chirpstack (ChirpStackHandler): The chirpstack handler.
        since (float): Time to check from.
        predicate (Callable[[Any], bool]): Predicate to look for.
        timeout (float): Timeout to wait.
        poll_interval (float, optional): Poll interval to for chirpstack.
                                         Defaults to 1.0.

    Raises:
        TimeoutError: If the predicate was not met within timeout.

    Returns:
        dict: The entry that matched the predicate.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for u in chirpstack.uplinks_since(since):
            for e in u.decoded.get("entries", []):
                if predicate(e):
                    return e
        time.sleep(poll_interval)
    msg = f"no entry matched predicate within {timeout}s"
    raise TimeoutError(msg)


def wait_for_alert(
    chirpstack: ChirpStackHandler,
    since: float,
    timeout: float,
    poll_interval: float = 1.0,
) -> dict:
    """Wait for an alert in the uplink.

    Args:
        chirpstack (ChirpStackHandler): The chirpstack handler.
        since (float): Time to check from.
        timeout (float): Timeout to wait.
        poll_interval (float, optional): Poll interval to for chirpstack.
                                         Defaults to 1.0.

    Raises:
        TimeoutError: If the predicate was not met within timeout.

    Returns:
        dict: The entry that matched the predicate.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for u in chirpstack.uplinks_since(since):
            if u.decoded.get("alert", []):
                return u
        time.sleep(poll_interval)
    msg = f"no entry matched predicate within {timeout}s"
    raise TimeoutError(msg)


@pytest.fixture
def chirpstack() -> any:
    """Chirpstack test fixture."""
    handler = ChirpStackHandler()
    handler.start()
    yield handler
    handler.stop()


@pytest.fixture
def node_cfgs() -> any:
    """Node config."""
    return FOG_NODES


@pytest.fixture
def otctl() -> any:
    """ot-ctl fixture."""
    return OtCtl(OT_CTL_PATH)
