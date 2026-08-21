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

import logging

import pytest

from config import Marker
from fog_fault_injector import FogFaultInjector
from test_helpers import (
    confirm_clean_baseline,
    eui64_to_decimal,
    leader_ipv6_address,
    wait_for_first_marker,
    wait_for_retransmission,
)

# How long the leader sleeps once paused in its egress window. Needs to be
# comfortably longer than the time it takes us to observe the pause marker
# and issue the hard-kill, or the node may resume and send before we kill it.
PAUSE_MS = 60_000

UPLINK_TIMEOUT = 120.0
PAUSE_MARKER_TIMEOUT = 120.0
NEW_LEADER_TIMEOUT = 60.0
REJOIN_TIMEOUT = 60.0
RETRANSMIT_TIMEOUT = 120.0

logger = logging.getLogger(__name__)


@pytest.mark.asyncio
async def test_leader_killed_while_paused_in_egress_window(
    chirpstack,
    otctl,
    fog_cluster,
    coap_context
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
    leader, original_uplink = await fog_cluster.current_leader(chirpstack,
                                                               UPLINK_TIMEOUT)
    others = fog_cluster.others_than(leader)
    logger.info("Leader identified: %s", leader.cfg.name)

    leader_addr = leader_ipv6_address(otctl, leader.cfg)
    injector = FogFaultInjector(node_ipv6=leader_addr, ctx=coap_context)

    try:
        logger.info(
            "Injecting a %s ms egress-window pause on %s...", PAUSE_MS, leader.cfg.name
        )
        try:
            response = await injector.set_fault(wait_time_ms=PAUSE_MS)
            logger.info("Fault PUT acknowledged: %s", response.code)
        except TimeoutError:
            pass  # the node pauses before it can ack the PUT - expected.

        await leader.wait_for(Marker.FAULT_HOOK_PAUSED, PAUSE_MARKER_TIMEOUT)
        logger.info("%s confirmed paused mid-egress-window.", leader.cfg.name)

        logger.info("Killing %s mid-uplink...", leader.cfg.name)
        leader.reset_and_hold()

        new_leader = await wait_for_first_marker(others,
                                                 Marker.BECAME_LEADER,
                                                 NEW_LEADER_TIMEOUT)
        logger.info("New leader elected: %s", new_leader.cfg.name)

        retransmitted = await wait_for_retransmission(
            chirpstack, chirpstack.mark(), original_uplink, RETRANSMIT_TIMEOUT
        )
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
        logger.info("Resuming %s...", leader.cfg.name)
        try:
            leader.resume()
            await leader.wait_for(Marker.BECAME_FOLLOWER, REJOIN_TIMEOUT)
            logger.info("%s rejoined as follower.", leader.cfg.name)
        except Exception as e:  # noqa: BLE001
            logger.info("WARNING: %s did not rejoin cleanly: %s", leader.cfg.name, e)
