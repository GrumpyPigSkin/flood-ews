"""Integration test.

Kill the LoRaWAN egress leader while it is paused mid-window by a fault
injection, and confirm a newly-elected leader retransmits the same logical
uplink.

    1. Wait for an uplink and read its `node_id` to identify the current leader.

    2. Resolve the leader's Thread ML-EID via the Raft peer-discovery service each
        fog node publishes in Thread Network Data (see
        NetworkService::register_local_service()), so we can reach its CoAP
        `fog_fault` resource.

    3. PUT a FogFaultInjector fault (`wait_time_ms=PAUSE_MS`) so the node pauses
        once it enters its egress window, long enough that we can kill it while
        it's stuck there.

    4. Wait for Marker.FAULT_HOOK_PAUSED ("FAULT:PAUSED_IN_WINDOW") in the
        leader's RTT log.

    5. Put the leader into reset to kill it.

    6. Confirm the remaining nodes elect a new leader (Marker.BECAME_LEADER) and
        that the SAME logical uplink is re-transmitted, this time from a different
        node's dev_eui.
"""

import asyncio
import logging
import time
from contextlib import ExitStack
from pathlib import Path

import pytest
from aiocoap import Context

from chirpstack_handler import Uplink
from config import Marker
from conftest import (
    eui64_to_decimal,
    leader_ipv6_address,
    same_uplink,
    wait_for_any_uplink,
    wait_for_first_marker,
)
from fog_fault_injector import FogFaultInjector
from jlink_node import JLinkNode

# How long the leader sleeps once paused in its egress window. Needs to be
# comfortably longer than the time it takes us to observe the pause marker
# and issue the hard-kill, or the node may resume and send before we kill it.
PAUSE_MS = 60_000

PAUSE_MARKER_TIMEOUT = 120.0
NEW_LEADER_TIMEOUT = 60.0
REJOIN_TIMEOUT = 60.0
RETRANSMIT_TIMEOUT = 120.0

logger = logging.getLogger(__name__)


@pytest.mark.asyncio
async def test_leader_killed_while_paused_in_egress_window(  # noqa: C901, PLR0915
    chirpstack,  # noqa: ANN001
    node_cfgs,  # noqa: ANN001
    otctl,  # noqa: ANN001
) -> None:
    """Integration test.

    Kill the LoRaWAN egress leader while it is paused mid-window by a fault
    injection, and confirm a newly-elected leader retransmits the same logical
    uplink.

        1. Wait for an uplink and read its `node_id` to identify the current leader.

        2. Resolve the leader's Thread ML-EID via the Raft peer-discovery service each
            fog node publishes in Thread Network Data (see
            NetworkService::register_local_service()), so we can reach its CoAP
            `fog_fault` resource.

        3. PUT a FogFaultInjector fault (`wait_time_ms=PAUSE_MS`) so the node pauses
            once it enters its egress window, long enough that we can kill it while
            it's stuck there.

        4. Wait for Marker.FAULT_HOOK_PAUSED ("FAULT:PAUSED_IN_WINDOW") in the
            leader's RTT log.

        5. Put the leader into reset to kill it.

        6. Confirm the remaining nodes elect a new leader (Marker.BECAME_LEADER) and
            that the SAME logical uplink is re-transmitted, this time from a different
            node's dev_eui.
    """
    nodes = [JLinkNode(cfg, index=i) for i, cfg in enumerate(node_cfgs)]

    with ExitStack() as stack:
        for n in nodes:
            stack.enter_context(n.session())
            time.sleep(1.0)  # noqa: ASYNC251
        try:
            logger.info("Reading EUI64 from all nodes...")
            eui_by_decimal = {}
            for n in nodes:
                dec_eui = eui64_to_decimal(n.cfg.device_eui)
                eui_by_decimal[dec_eui] = n
                logger.info("%s: eui64=%s -> %s", n.cfg.name, n.cfg.device_eui, dec_eui)

            logger.info("Waiting for an uplink to identify the current leader...")
            mark = chirpstack.mark()
            uplinks = await asyncio.to_thread(
                lambda: wait_for_any_uplink(chirpstack, mark, timeout=120.0)
            )
            original_uplink = uplinks[-1]
            leader_node_id = int(original_uplink.decoded["node_id"])
            leader = eui_by_decimal.get(leader_node_id)
            assert leader is not None, (
                f"node_id {leader_node_id} did not match any known node "
                f"(known: {list(eui_by_decimal)})"
            )
            others = [n for n in nodes if n is not leader]
            logger.info("Leader identified: %s", leader.cfg.name)

            for n in nodes:
                n.rtt.drain_new()

            logger.info("Resolving %s's Thread address...", leader.cfg.name)
            leader_addr = leader_ipv6_address(otctl, leader.cfg)
            logger.info("%s address: %s", leader.cfg.name, leader_addr)

            ctx = await Context.create_client_context()
            injector = FogFaultInjector(node_ipv6=leader_addr, ctx=ctx)
            retransmitted = None
            try:
                logger.info(
                    "Injecting a %s ms egress-window pause on %s...",
                    PAUSE_MS,
                    leader.cfg.name,
                )
                try:
                    response = await injector.set_fault(wait_time_ms=PAUSE_MS)
                    logger.info("Fault PUT acknowledged: %s", response.code)
                except TimeoutError:
                    pass

                logger.info(
                    "Waiting up to %s s for %s...",
                    PAUSE_MARKER_TIMEOUT,
                    Marker.FAULT_HOOK_PAUSED,
                )
                await asyncio.to_thread(
                    leader.rtt.wait_for, Marker.FAULT_HOOK_PAUSED, PAUSE_MARKER_TIMEOUT
                )
                logger.info("%s confirmed paused mid-egress-window.", leader.cfg.name)

                logger.info("Killing %s mid-uplink...", leader.cfg.name)
                leader.reset_and_hold()

                logger.info("Waiting for a new leader to be elected...")
                new_leader = await asyncio.to_thread(
                    wait_for_first_marker,
                    others,
                    Marker.BECAME_LEADER,
                    NEW_LEADER_TIMEOUT,
                )
                logger.info("New leader elected: %s", new_leader.cfg.name)

                logger.info(
                    "Waiting up to %s s for the same uplink to be "
                    "retransmitted by a different node...",
                    RETRANSMIT_TIMEOUT,
                )
                retransmit_mark = chirpstack.mark()

                def _find_retransmit() -> Uplink:
                    deadline = time.monotonic() + RETRANSMIT_TIMEOUT
                    while time.monotonic() < deadline:
                        for u in chirpstack.uplinks_since(retransmit_mark):
                            if u.dev_eui != original_uplink.dev_eui and same_uplink(
                                u.decoded, original_uplink.decoded
                            ):
                                return u
                        time.sleep(0.5)
                    msg = (
                        "no matching retransmission of the paused uplink seen "
                        f"within {RETRANSMIT_TIMEOUT}s"
                    )
                    raise TimeoutError(msg)

                retransmitted = await asyncio.to_thread(_find_retransmit)
                logger.info(
                    "Retransmission confirmed from dev_eui=%s (originally %s)",
                    retransmitted.dev_eui,
                    original_uplink.dev_eui,
                )
                assert retransmitted.dev_eui != original_uplink.dev_eui
                assert int(retransmitted.decoded.get("node_id")) == eui64_to_decimal(
                    new_leader.cfg.device_eui
                ), "retransmitting device's node_id didn't match the elected new leader"

            finally:
                # Bring the dead node back.
                logger.info("Resuming %s...", leader.cfg.name)
                try:
                    leader.resume()
                    await asyncio.to_thread(
                        leader.rtt.wait_for, Marker.BECAME_FOLLOWER, REJOIN_TIMEOUT
                    )
                    logger.info("%s rejoined as follower.", leader.cfg.name)
                except Exception as e:  # noqa: BLE001
                    logger.info(
                        "WARNING: %s did not rejoin cleanly: %s", leader.cfg.name, e
                    )

                try:
                    await injector.clear()
                    logger.info("Cleared fault on %s.", leader.cfg.name)
                except Exception as e:  # noqa: BLE001
                    logger.info(
                        "WARNING: could not clear fault on %s "
                        "(may still be unreachable): %s",
                        leader.cfg.name,
                        e,
                    )

                await ctx.shutdown()
        finally:
            Path("test_egress").mkdir(exist_ok=True)  # noqa: ASYNC240
            for n in nodes:
                n.rtt.dump_to_file(f"test_egress/{n.cfg.name}.log")
                logger.info(
                    "Dumped RTT history for %s to test_egress/%s.log",
                    n.cfg.name,
                    n.cfg.name,
                )
