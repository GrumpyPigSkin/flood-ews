"""Integration Test.

Tests that killing a leader results in another node becoming leader, and that
the old leader doesn't take over as leader.

    1. Find the leader by waiting for the latest uplink.

    2. Kill the leader using the JLink connection.

    3. Wait one of the other nodes to take over as LEADER.

    4. Revive the old leader and wait for it to come back as Follower.
"""

import logging

import pytest

from config import Marker
from test_helpers import (
    confirm_clean_baseline,
    wait_for_first_marker,
)

logger = logging.getLogger(__name__)

STATE_CHANGE_TIMEOUT = 60.0
UPLINK_TIMEOUT = 120.0
CLEAN_ENTRY_TIMEOUT = 300.0


@pytest.mark.asyncio
async def test_leader_failover(chirpstack, fog_cluster) -> None:  # noqa: ANN001
    """Integration Test.

    Tests that killing a leader results in another node becoming leader, and that
    the old leader doesn't take over as leader.

        1. Find the leader by waiting for the latest uplink.

        2. Kill the leader using the JLink connection.

        3. Wait one of the other nodes to take over as LEADER.

        4. Revive the old leader and wait for it to come back as Follower.
    """
    leader, _ = await fog_cluster.current_leader(chirpstack, UPLINK_TIMEOUT)
    others = fog_cluster.others_than(leader)
    logger.info("Leader identified: %s", leader.cfg.name)

    logger.info("Resetting leader (%s) and holding it in reset...", leader.cfg.name)
    leader.reset_and_hold()

    new_leader = await wait_for_first_marker(others,
                                             Marker.BECAME_LEADER,
                                             STATE_CHANGE_TIMEOUT)
    logger.info("New leader elected: %s", new_leader.cfg.name)

    logger.info("Releasing %s from reset...", leader.cfg.name)
    leader.resume()
    await leader.wait_for(Marker.BECAME_FOLLOWER, STATE_CHANGE_TIMEOUT)
    logger.info("Failover confirmed: old leader rejoined as follower.")
    await confirm_clean_baseline(chirpstack, CLEAN_ENTRY_TIMEOUT)
