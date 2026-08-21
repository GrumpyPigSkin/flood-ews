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

import logging
from contextlib import AsyncExitStack

import pytest

from sensor_fault_injector import SensorFaultInjector
from test_helpers import confirm_clean_baseline, discover_sed_pool, wait_for_alert

BASELINE_WINDOW = 150.0  # watch ~2.5 reporting cycles for a clean start
ALERT_WAIT = 300.0  # ~5 reporting cycles alert but we should get it on the next cycle.
CLEAR_ALERT_TIMEOUT = 660.0  # ~6 reporting cycles to become "GOOD" again.
HIGH_WATER_LVL_MM = 1600  # Water level to override.

logger = logging.getLogger(__name__)


@pytest.mark.asyncio
async def test_alert_triggered(chirpstack, coap_context, otctl) -> None:  # noqa: ANN001
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
    await confirm_clean_baseline(chirpstack, BASELINE_WINDOW)
    sed_pool = discover_sed_pool(otctl)

    fault_mark = chirpstack.mark()
    async with AsyncExitStack() as faults:
        for sed in sed_pool:
            sfi = SensorFaultInjector(node_ipv6=sed["address"], ctx=coap_context)
            await faults.enter_async_context(
                sfi.active(water_level_mm=HIGH_WATER_LVL_MM)
                )

        entry = await wait_for_alert(chirpstack, fault_mark, ALERT_WAIT)
        logger.info("Alert received: %s", entry)

    await confirm_clean_baseline(chirpstack, CLEAR_ALERT_TIMEOUT)
