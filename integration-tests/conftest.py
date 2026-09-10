"""Test fixtures used throughout testing."""

import os

import pytest
from aiocoap import Context

from chirpstack_handler import ChirpStackHandler
from config import (
    CHANNEL,
    FOG_NODES,
    GATEWAY_URL,
    NETWORK_KEY,
    OT_CTL_PATH,
    PANID,
    XPANID,
)
from fog_cluster import FogCluster
from gateway_api_client import GatewayApiClient
from ot_ctl import OtCtl


@pytest.fixture
def chirpstack() -> any:
    """Chirpstack test fixture."""
    handler = ChirpStackHandler()
    handler.start()
    yield handler
    handler.stop()


@pytest.fixture
def node_cfgs() -> any:
    """Node config."""
    return FOG_NODES


@pytest.fixture
def otctl() -> any:
    """ot-ctl fixture, ensures we are connected to the thread network."""
    ctl = OtCtl(OT_CTL_PATH)
    if not ctl.is_attached():
        ctl.connect_to_thread_network(NETWORK_KEY, CHANNEL, PANID, XPANID)
    if not ctl.is_attached():
        pytest.fail("Failed to attach to Thread network")
    return ctl


@pytest.fixture
async def coap_context() -> any:
    """A CoAP client context.

    Shuts down automatically at the end of the test.
    """
    ctx = await Context.create_client_context()
    yield ctx
    await ctx.shutdown()


@pytest.fixture
def fog_cluster(node_cfgs: any, request: pytest.FixtureRequest) -> any:
    """A connected FogCluster.

    Automatically writes history at the end of the test.
    """
    cluster = FogCluster(node_cfgs, log_dir=request.node.name)
    with cluster.connected():
        yield cluster

@pytest.fixture(scope="session")
def gateway_client() -> GatewayApiClient:
    """Connect to the gateway."""
    client = GatewayApiClient(GATEWAY_URL)
    password = os.environ["GATEWAY_PASSWORD"]
    client.login(password)
    return client
