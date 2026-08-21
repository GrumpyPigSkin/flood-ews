"""Integration Test.

Test that a reading with a duplicate sequence id gets dropped when received by the
Fog layer.
    1. Inject a sequence lower than the last one for a random sensor.
    2. Check the fog layer rejects the reading.
"""

import logging
import random

import pytest

from config import Marker
from sensor_fault_injector import SensorFaultInjector
from test_helpers import confirm_clean_baseline, discover_sed_pool

TIMEOUT = 120.0

logger = logging.getLogger(__name__)

@pytest.mark.asyncio
async def test_duplicate_seq_rejected(
    chirpstack,
    otctl,
    fog_cluster,
    coap_context) -> None:
    """Integration Test.

    Test that a reading with a duplicate sequence id gets dropped when received by the
    Fog layer.

        1. Inject a sequence lower than the last one for a random sensor.

        2. Check the fog layer rejects the reading.
    """
    await confirm_clean_baseline(chirpstack, TIMEOUT)
    sed_pool = discover_sed_pool(otctl)
    target = random.choice(sed_pool)  # noqa: S311
    logger.info("Selected target %s", target["address"])
    sfi = SensorFaultInjector(node_ipv6=target["address"], ctx=coap_context)
    leader, _ = await fog_cluster.current_leader(chirpstack, TIMEOUT)

    async with sfi.active(water_level_mm=500, seq=1):
        logger.info(
            "Waiting up to %s s for a message SEQ_ERR to appear on a fog node.",
            TIMEOUT
        )
        await leader.wait_for(Marker.SEQ_ERR, TIMEOUT)
        logger.info("%s saw marker: %s", leader, Marker.SEQ_ERR)

    await confirm_clean_baseline(chirpstack, TIMEOUT)
