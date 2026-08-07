"""Inject an error into the edge_fault end point.

This is used to spoof messages by injecting bad sensor readings and passing them
to the fog layer to test the outlier vote logic.
"""

import asyncio

from aiocoap import Code, Context, Message
from aiocoap.numbers.types import Type

from sensor_fault_wire import DetailQual, Validity, pack_clear, pack_fault


class SensorFaultInjector:
    """Fault-injection into the sensor node's `edge_fault` CoAP resource.

    The firmware (edge::fault::FaultInjection) registers ONE resource at the URI
    `edge_fault` and expects a whole FaultMessage as the payload. It stores the
    message and, while m_active is set, overrides every outgoing reading in
    CoapService::send_sensor_data. So the fault is sticky until cleared. This
    resource only exists in a build with ENABLE_FAULT_INJECTION defined.
    """

    FAULT_URI = "edge_fault"

    def __init__(self, node_ipv6: str, ctx: Context) -> None:
        """Initialise SensorFaultInjector.

        Args:
            node_ipv6 (str): The ip address of the node to send the fault to.
            ctx (Context): The reusable context.
        """
        self._addr = node_ipv6
        self._ctx = ctx

    async def _put(self, payload: bytes, *, timeout: float = 30.0) -> any:  # noqa: ASYNC109
        """Send the message to the fog node over CoAP.

        Args:
            payload (bytes): The packed FogFaultMessage
            timeout (float, optional): Timeout for the message. Defaults to 2.0.

        Raises:
            RuntimeError: On response not successful.

        Returns:
            any: The response from the request.
        """
        msg = Message(
            code=Code.PUT,
            mtype=Type.CON,
            payload=payload,
            uri=f"coap://[{self._addr}]/{self.FAULT_URI}",
        )
        response = await asyncio.wait_for(
            self._ctx.request(msg).response, timeout=timeout
        )
        if not response.code.is_successful():
            msg_0 = f"Fault injection PUT to {self._addr} failed: {response.code}"
            raise RuntimeError(msg_0)
        return response

    async def set_fault(
        self,
        *,
        water_level_mm: int,
        validity: Validity = Validity.GOOD,
        detail: DetailQual = DetailQual.NONE,
        seq: int = 0,
        timestamp: int = 0,
    ) -> any:
        """Activate a fault on the sensor reading.

        Every subsequent reading from this node is overridden with these values
        until cleared.

        Args:
            water_level_mm (int): The new water level in millilitres
            validity (Validity, optional): The new validity. Defaults to Validity.GOOD.
            detail (DetailQual, optional): The new detail. Defaults to DetailQual.NONE.
            seq (int, optional): The overridden sequence ID. Defaults to 0.
            timestamp (int, optional): The timestamp. Defaults to 0.

        Returns:
            any: The response code.
        """
        payload = pack_fault(
            eui=0,
            timestamp=timestamp,
            water_level_mm=water_level_mm,
            validity=validity,
            detail=detail,
            seq=seq,
            active=True,
        )
        return await self._put(payload)

    async def set_next_reading(self, water_level_mm: int) -> any:
        """Override just water quality.

        Args:
            water_level_mm (int): The new water level.

        Returns:
            any: The response code.
        """
        return await self.set_fault(water_level_mm=water_level_mm)

    async def force_quality(
        self, validity: Validity, detail: DetailQual, water_level_mm: int = 0
    ) -> any:
        """Force IEC 61850 quality flags on the overridden reading.

        Args:
            validity (Validity): The new validity. Defaults to Validity.GOOD.
            detail (DetailQual): The new detail. Defaults to DetailQual.NONE.
            water_level_mm (int, optional): The new water level in millilitres.

        Returns:
            any: The response code.
        """
        return await self.set_fault(
            water_level_mm=water_level_mm, validity=validity, detail=detail
        )

    async def clear(self) -> any:
        """Deactivate the fault so the node resumes real sensor readings."""
        return await self._put(pack_clear())
