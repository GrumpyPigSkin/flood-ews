"""Integration Test.

Test that a reading with a bash hash gets dropped when received by the
Fog layer.
    1. Tell the sensor to omit the hash.
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
async def test_bad_hash_is_rejected(
    chirpstack,
    otctl,
    fog_cluster,
    coap_context) -> None:
    """Integration Test.

    Test that a reading with a bash hash gets dropped when received by the
    Fog layer.

        1. Tell the sensor to omit the hash.

        2. Check the fog layer rejects the reading.
    """
    await confirm_clean_baseline(chirpstack, TIMEOUT)
    sed_pool = discover_sed_pool(otctl)
    target = random.choice(sed_pool)  # noqa: S311
    logger.info("Selected target %s", target["address"])
    sfi = SensorFaultInjector(node_ipv6=target["address"], ctx=coap_context)
    leader, _ = await fog_cluster.current_leader(chirpstack, TIMEOUT)

    async with sfi.active(water_level_mm=500, sign=True):
        logger.info(
            "Waiting up to %s s for a message SIGN_ERR to appear on a fog node.",
            TIMEOUT
        )
        await leader.wait_for(Marker.SIGN_ERR, TIMEOUT)
        logger.info("%s saw marker: %s", leader, Marker.SIGN_ERR)

    await confirm_clean_baseline(chirpstack, TIMEOUT)
