"""Integration Test.

Tests that killing a leader results in another node becoming leader, and that
the old leader doesn't take over as leader.

    1. Find the leader by waiting for the latest uplink.

    2. Kill the leader using the JLink connection.

    3. Wait one of the other nodes to take over as LEADER.

    4. Revive the old leader and wait for it to come back as Follower.
"""

import asyncio
import logging
import time
from contextlib import ExitStack
from pathlib import Path

import pytest

from config import Marker
from conftest import (
    eui64_to_decimal,
    wait_for_any_uplink,
    wait_for_first_marker,
)
from jlink_node import JLinkNode

logger = logging.getLogger(__name__)

STATE_CHANGE_TIMEOUT = 60.0
UPLINK_TIMEOUT = 120.0


@pytest.mark.asyncio
async def test_leader_failover(chirpstack, node_cfgs) -> None:  # noqa: ANN001
    """Integration Test.

    Tests that killing a leader results in another node becoming leader, and that
    the old leader doesn't take over as leader.

        1. Find the leader by waiting for the latest uplink.

        2. Kill the leader using the JLink connection.

        3. Wait one of the other nodes to take over as LEADER.

        4. Revive the old leader and wait for it to come back as Follower.
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

            logger.info("Identifying current leader via latest LoRaWAN uplink...")
            mark = chirpstack.mark()
            uplinks = await asyncio.to_thread(
                lambda: wait_for_any_uplink(chirpstack, mark, timeout=UPLINK_TIMEOUT)
            )
            leader_node_id = int(uplinks[-1].decoded["node_id"])
            leader = eui_by_decimal.get(leader_node_id)
            assert leader is not None, (
                f"node_id {leader_node_id} did not match any known node "
                f"(known: {list(eui_by_decimal)})"
            )
            others = [n for n in nodes if n is not leader]
            logger.info("Leader identified: %s", leader.cfg.name)

            for n in nodes:
                n.rtt.drain_new()

            logger.info(
                "Resetting leader (%s) and holding it in reset...", leader.cfg.name
            )
            leader.reset_and_hold()

            logger.info("Waiting for one of the remaining nodes to become leader...")
            new_leader = await asyncio.to_thread(
                wait_for_first_marker,
                others,
                Marker.BECAME_LEADER,
                STATE_CHANGE_TIMEOUT,
            )
            logger.info("New leader elected: %s", new_leader.cfg.name)

            logger.info("Releasing %s from reset...", leader.cfg.name)
            leader.resume()

            logger.info("Waiting for %s to rejoin as follower...", leader.cfg.name)
            await asyncio.to_thread(
                leader.rtt.wait_for, Marker.BECAME_FOLLOWER, STATE_CHANGE_TIMEOUT
            )
            logger.info("Failover confirmed: old leader rejoined as follower.")
        finally:
            Path("test_leader_failover").mkdir(exist_ok=True)  # noqa: ASYNC240
            for n in nodes:
                n.rtt.dump_to_file(f"test_leader_failover/{n.cfg.name}.log")
                logger.info(
                    "Dumped RTT history for %s to test_leader_failover/%s.log",
                    n.cfg.name,
                    n.cfg.name,
                )
