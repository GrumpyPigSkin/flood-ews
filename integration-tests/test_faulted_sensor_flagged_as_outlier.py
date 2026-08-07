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

import asyncio
import logging
import random

import pytest
from aiocoap import Context

from config import CHANNEL, NETWORK_KEY, PANID, XPANID
from conftest import wait_for_all_good, wait_for_entry_state_by_predicate
from sensor_fault_injector import SensorFaultInjector

BASELINE_WINDOW = 150.0  # watch ~2.5 reporting cycles for a clean start
OUTLIER_DETECT_TIMEOUT = 150.0  # ~2.5 reporting cycles for QUESTIONABLE + OUTLIER
INVALID_DETECT_TIMEOUT = 330.0  # ~5.5 reporting cycles further for INVALID
VALID_DETECT_TIMEOUT = 660.0  # 10 reporting cycles to become "GOOD" again.

logger = logging.getLogger(__name__)


@pytest.mark.asyncio
async def test_faulted_sensor_flagged_as_outlier(chirpstack, otctl) -> None:  # noqa: ANN001
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

    target = random.choice(pool)  # noqa: S311
    logger.info("Selected target %s", target["address"])

    ctx = await Context.create_client_context()
    sfi = SensorFaultInjector(node_ipv6=target["address"], ctx=ctx)
    entry = None
    try:
        logger.info("Sending fault injection PUT...")
        fault_mark = chirpstack.mark()
        expected_water_lvl = 1000
        response = await sfi.set_fault(
            water_level_mm=expected_water_lvl,
        )
        logger.info("Fault PUT acknowledged: %s", response.code)

        logger.info(
            "Waiting up to %s s for outlier + QUESTIONABLE...", OUTLIER_DETECT_TIMEOUT
        )
        entry = await asyncio.to_thread(
            wait_for_entry_state_by_predicate,
            chirpstack,
            fault_mark,
            lambda e: (
                e.get("water_level_mm") == expected_water_lvl
                and e.get("outlier") is True
            ),
            OUTLIER_DETECT_TIMEOUT,
        )
        logger.info("outlier + QUESTIONABLE confirmed: %s", entry)

        logger.info(
            "Waiting up to %s s for validity to become INVALID...",
            INVALID_DETECT_TIMEOUT,
        )
        fault_mark = chirpstack.mark()
        entry = await asyncio.to_thread(
            wait_for_entry_state_by_predicate,
            chirpstack,
            fault_mark,
            lambda e: (
                e.get("device_eui") == entry["device_eui"]
                and e.get("water_level_mm") == expected_water_lvl
                and e.get("validity") == "INVALID"
            ),
            INVALID_DETECT_TIMEOUT,
        )
        logger.info("INVALID confirmed: %s", entry)

    finally:
        logger.info("Clearing fault...")
        await sfi.clear()

        if entry is not None:
            logger.info(
                "Waiting up to %s s for reading to become GOOD...", VALID_DETECT_TIMEOUT
            )
            fault_mark = chirpstack.mark()
            entry = await asyncio.to_thread(
                wait_for_entry_state_by_predicate,
                chirpstack,
                fault_mark,
                lambda e: (
                    e.get("device_eui") == entry["device_eui"]
                    and e.get("validity") == "GOOD"
                ),
                VALID_DETECT_TIMEOUT,
            )

        await ctx.shutdown()
        logger.info("Fault cleared.")
