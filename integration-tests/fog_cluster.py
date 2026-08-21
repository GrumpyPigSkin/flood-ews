"""Test helpers for accessing fog nodes in a cluster."""

import logging
import time
from contextlib import ExitStack, contextmanager
from pathlib import Path

from chirpstack_handler import ChirpStackHandler, Uplink
from config import NodeCfg
from jlink_node import JLinkNode
from test_helpers import eui64_to_decimal

logger = logging.getLogger(__name__)

class FogCluster:
    """Test helpers for accessing fog nodes in a cluster."""

    def __init__(self, cfgs: list[NodeCfg], log_dir: str) -> None:
        """Initialise FogCluster.

        Args:
            cfgs (list[NodeCfg]): List of node configurations.
            log_dir (str): The directory to write logs to.
        """
        self.nodes = [JLinkNode(cfg, index=i) for i, cfg in enumerate(cfgs)]
        self.log_dir = log_dir
        self._by_id = {eui64_to_decimal(n.cfg.device_eui): n for n in self.nodes}

    def node_for_id(self, node_eui: int) -> JLinkNode:
      """Get a node by it's EUI-64.

      Args:
          node_eui (int): The EUI-64 to look for.

      Raises:
          AssertionError: Is the node could not be found.

      Returns:
          JLinkNode: The JLinkNode for this node.
      """
      node = self._by_id.get(node_eui)
      if node is None:
          msg = (
                  f"node_id {node_eui} did not match any known node "
                  f"(known: {list(self._by_id)})"
              )
          raise AssertionError(msg)
      return node

    def others_than(self, node: JLinkNode) -> list[JLinkNode]:
        """Get all nodes other than the one given.

        Args:
            node (JLinkNode): The node to exclude.

        Returns:
            list[JLinkNode]: The other nodes.
        """
        return [n for n in self.nodes if n is not node]

    async def current_leader(self, chirpstack: ChirpStackHandler, timeout: float
        ) -> tuple[JLinkNode, Uplink]:
        """Identify the current leader from the LoRaWAN uplink.

        Args:
            chirpstack (ChirpStackHandler): The chirpstack handler to watch.
            timeout (float): How long to wait.

        Returns:
            tuple[JLinkNode, Uplink]: The leader node and the uplink.
        """
        uplink = await chirpstack.wait_for_uplink(chirpstack.mark(), timeout)
        return self.node_for_id(int(uplink.decoded["node_id"])), uplink

    @contextmanager
    def connected(self) -> any:
        """Open a J-Link session to every node; always capture RTT logs on exit.

        Also discards any RTT backlog from before the session opened, so the
        first `wait_for` a test does only sees markers from this run.
        """
        with ExitStack() as stack:
            for n in self.nodes:
                stack.enter_context(n.session())
                time.sleep(1.0)  # let each RTT control block settle before the next
            for n in self.nodes:
                n.rtt.drain_new()
            try:
                yield self
            finally:
                Path(self.log_dir).mkdir(exist_ok=True)
                for n in self.nodes:
                    path = f"{self.log_dir}/{n.cfg.name}.log"
                    n.rtt.dump_to_file(path)
                    logger.info("Dumped RTT history for %s to %s", n.cfg.name, path)
