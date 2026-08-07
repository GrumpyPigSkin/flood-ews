"""Integration Test.

Test that when all nodes report the water level is higher than the trigger
level, an alert is triggered.

  1. Collect all the seds.

  2. Override their water level so it is above threshold, the default threshold
     is 1500mm.

  3. Wait for the alert to come back in the uplink.

  4. Remove the override.

  5. Wait for the the alert to clear, we need to see 3 clean cycles before a
     clear.
"""

import asyncio
import logging

import pytest
from aiocoap import Context

from config import CHANNEL, NETWORK_KEY, PANID, XPANID
from conftest import wait_for_alert, wait_for_all_good
from sensor_fault_injector import SensorFaultInjector

BASELINE_WINDOW = 150.0  # watch ~2.5 reporting cycles for a clean start
ALERT_WAIT = 300.0  # ~5 reporting cycles alert but we should get it on the next cycle.
CLEAR_ALERT_TIMEOUT = 660.0  # ~6 reporting cycles to become "GOOD" again.
HIGH_WATER_LVL_MM = 1600  # Water level to override.

logger = logging.getLogger(__name__)


@pytest.mark.asyncio
async def test_alert_triggered(chirpstack, otctl) -> None:  # noqa: ANN001
    """Integration Test.

    Test that when all nodes report the water level is higher than the trigger
    level, an alert is triggered.

      1. Collect all the seds.

      2. Override their water level so it is above threshold, the default threshold
        is 1500mm.

      3. Wait for the alert to come back in the uplink.

      4. Remove the override.

      5. Wait for the the alert to clear, we need to see 3 clean cycles before a
        clear.
    """
    logger.info("Checking Thread attachment...")
    if not otctl.is_attached():
        otctl.connect_to_thread_network(NETWORK_KEY, CHANNEL, PANID, XPANID)
    if not otctl.is_attached():
        msg = "Failed to attach to Thread network"
        raise RuntimeError(msg)

    logger.info("Waiting up to %s s for a clean baseline...", BASELINE_WINDOW)
    baseline_mark = chirpstack.mark()
    await asyncio.to_thread(
        wait_for_all_good, chirpstack, baseline_mark, BASELINE_WINDOW
    )

    logger.info("Baseline confirmed clean.")

    logger.info("Discovering fault-injection targets...")
    seds = otctl.get_seds()
    pool = [{"role": "sed", **s} for s in seds]
    assert pool, "no fault-injection targets found"

    ctx = await Context.create_client_context()

    try:
        logger.info("Sending fault injection PUT...")
        fault_mark = chirpstack.mark()

        for sed in pool:
            sfi = SensorFaultInjector(node_ipv6=sed["address"], ctx=ctx)
            response = await sfi.set_fault(
                water_level_mm=HIGH_WATER_LVL_MM,
            )
            logger.info("Fault PUT acknowledged: %s", response.code)

        logger.info("Waiting %s s for alert...", ALERT_WAIT)

        entry = await asyncio.to_thread(
            wait_for_alert,
            chirpstack,
            fault_mark,
            ALERT_WAIT,
        )

        logger.info("Alert received, %s", entry)

    finally:
        logger.info("Clearing errors...")

        for sed in pool:
            sfi = SensorFaultInjector(node_ipv6=sed["address"], ctx=ctx)
            response = await sfi.clear()
            logger.info("Fault clear acknowledged: %s", response.code)

        baseline_mark = chirpstack.mark()
        await asyncio.to_thread(
            wait_for_all_good, chirpstack, baseline_mark, CLEAR_ALERT_TIMEOUT
        )
