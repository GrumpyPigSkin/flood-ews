"""Integration Test.

Test that a fault sensor is marked as an outlier, even if the sensor thinks
it is sending good values.

    1. This tests the whole system end to end:

    2. A fault is injected into a random sensor over CoAP.

    3. The faulty sensor should be marked as "QUESTIONABLE" and and an outlier
    immediately by the fog layer.

    4. After around 5 bad windows, the fog layer should mark the reading as
    "INVALID".

    5. After the sensor becomes invalid, the fault is then cleared, after ~6
    consecutive clear windows the sensor is readmitted and becomes good again.
"""

import logging
import random

import pytest

from sensor_fault_injector import SensorFaultInjector
from test_helpers import (
    confirm_clean_baseline,
    discover_sed_pool,
    wait_for_entry_state_by_predicate,
)

BASELINE_WINDOW = 150.0  # watch ~2.5 reporting cycles for a clean start
OUTLIER_DETECT_TIMEOUT = 150.0  # ~2.5 reporting cycles for QUESTIONABLE + OUTLIER
INVALID_DETECT_TIMEOUT = 330.0  # ~5.5 reporting cycles further for INVALID
VALID_DETECT_TIMEOUT = 660.0  # 10 reporting cycles to become "GOOD" again.
FAULT_WATER_LVL_MM = 1600  # Water level to override.

logger = logging.getLogger(__name__)


@pytest.mark.asyncio
async def test_faulted_sensor_flagged_as_outlier(
    chirpstack,
    otctl,
    coap_context) -> None:
    """Integration Test.

    Test that a fault sensor is marked as an outlier, even if the sensor thinks
    it is sending good values.

        1. This tests the whole system end to end:

        2. A fault is injected into a random sensor over CoAP.

        3. The faulty sensor should be marked as "QUESTIONABLE" and and an outlier
        immediately by the fog layer.

        4. After around 5 bad windows, the fog layer should mark the reading as
        "INVALID".

        5. After the sensor becomes invalid, the fault is then cleared, after ~6
        consecutive clear windows the sensor is readmitted and becomes good again.
    """
    await confirm_clean_baseline(chirpstack, BASELINE_WINDOW)
    sed_pool = discover_sed_pool(otctl)
    target = random.choice(sed_pool)  # noqa: S311
    logger.info("Selected target %s", target["address"])
    sfi = SensorFaultInjector(node_ipv6=target["address"], ctx=coap_context)

    entry = None
    uplink = None
    async with sfi.active(water_level_mm=FAULT_WATER_LVL_MM):
        uplink, entry = await wait_for_entry_state_by_predicate(
            chirpstack,
            chirpstack.mark(),
            lambda e: (
                e.get("water_level_mm") == FAULT_WATER_LVL_MM and e.get("outlier") is True
            ),
            OUTLIER_DETECT_TIMEOUT,
        )

        assert(uplink.get("alert") is False)

        logger.info("outlier confirmed: %s", entry)

        logger.info(
            "Waiting up to %s s for validity to become INVALID...",
            INVALID_DETECT_TIMEOUT,
        )
        uplink, entry = await wait_for_entry_state_by_predicate(
            chirpstack,
            chirpstack.mark(),
            lambda e: (
                e.get("device_eui") == entry["device_eui"]
                and e.get("water_level_mm") == FAULT_WATER_LVL_MM
                and e.get("validity") == "INVALID"
            ),
            INVALID_DETECT_TIMEOUT,
        )
        assert(uplink.get("alert") is False)
        logger.info("INVALID confirmed: %s", entry)

    if entry is not None:
        await wait_for_entry_state_by_predicate(
            chirpstack,
            chirpstack.mark(),
            lambda e: (e.get("device_eui") == entry["device_eui"]
                       and e.get("validity") == "GOOD"),
            VALID_DETECT_TIMEOUT,
        )
        logger.info("Reading for %s recovered to GOOD.", entry["device_eui"])
