"""Inject a fault into the fault_fog CoAP endpoint."""

import asyncio

from aiocoap import Code, Context, Message
from aiocoap.numbers.types import Type

from fault_egress_wait_wire import pack_fog_clear, pack_fog_fault


class FogFaultInjector:
    """Fault-injection into the sensor node's `fog_fault` CoAP resource.

    The firmware (fog::fault::FaultInjection) registers ONE resource at the URI
    `fog_fault` and expects a whole FaultMessage as the payload. The fog fault
    unlink the edge fault is not sticky on the device, it activates once and
    clears itself. This is used to mark a synchronisation point between the test
    and the firmware for shutdown. This resource only exists in a build with
    ENABLE_FAULT_INJECTION defined.
    """

    FAULT_URI = "fog_fault"

    def __init__(self, node_ipv6: str, ctx: Context) -> None:
        """Initialise FogFaultInjector.

        Args:
            node_ipv6 (str): The ip address of the node to send the fault to.
            ctx (Context): The reusable context.
        """
        self._addr = node_ipv6
        self._ctx = ctx

    async def _put(self, payload: bytes, *, timeout: float = 2.0) -> any:  # noqa: ASYNC109
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
            mtype=Type.NON,
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
        wait_time_ms: int,
    ) -> any:
        """Activate the fault on the node, this fault is none-sticky.

        Args:
            wait_time_ms (int): The wait time to pause for in milliseconds.

        Returns:
            any: The response code.
        """
        payload = pack_fog_fault(
            time_sleep_ms=wait_time_ms,
            active=True,
        )
        return await self._put(payload)

    async def clear(self) -> any:
        """Clear the fault."""
        return await self._put(pack_fog_clear())
