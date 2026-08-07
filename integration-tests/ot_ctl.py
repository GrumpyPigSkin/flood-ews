"""Handle communications with the ot-ctl software.

Communicates over spinel with the nRF52840 USB dongle for interrogating the
thread network for testing.
"""

import logging
import re
import shutil
import subprocess

logger = logging.getLogger(__name__)

LOCAL_MULTICAST_ADDR = "ff03::2"

# Resolve absolute path to sudo
sudo_path = shutil.which("sudo") or "/usr/bin/sudo"


class OtCtl:
    """Handles communications with ot-ctl.

    Used to setup connection to the thread network, find SSEDs and get router
    information for testing.
    """

    def __init__(self, exe_path: list[str]) -> None:
        """Initialise the OtCtl class.

        Args:
            exe_path (list[str]): path to the exe
        """
        self._path = exe_path

    def run_command(self, *args: str) -> str:
        """Run a command with the ot-ctl software.

        Raises:
            RuntimeError: Raised when running ot-ctl runs a none 0 return code.

        Returns:
            str: The resulting stdout
        """
        result = subprocess.run(  # noqa: S603
            [sudo_path, "-n", *self._path, *list(args)],
            capture_output=True,
            text=True,
            check=True,
        )
        if result.returncode != 0:
            logger.info(result.stdout)
            logger.warning(result.stderr)
            msg = f"ot-ctl {' '.join(args)} failed with code {result.returncode}"
            raise RuntimeError(msg)
        return result.stdout

    def connect_to_thread_network(
        self,
        network_key: str,
        channel: str,
        panid: str,
        xpandid: str,
    ) -> None:
        """Connect to the thread network.

        Args:
            network_key (str): The network key
            channel (str): The channel to attach to
            panid (str): Then PAN ID in hex
            xpandid (str): The XPANID
        """
        self.run_command("dataset", "networkkey", network_key)
        self.run_command("dataset", "channel", channel)
        self.run_command("dataset", "panid", panid)
        self.run_command("dataset", "extpanid", xpandid)
        self.run_command("dataset", "commit", "active")
        self.run_command("ifconfig", "up")
        self.run_command("thread", "start")

    def is_attached(self) -> bool:
        """Check if the daemon is already attached to a network.

        Returns:
            bool: True if it is.
        """
        raw = self.run_command("state")
        first_line = raw.strip().splitlines()[0].strip() if raw.strip() else ""
        return first_line not in ("detached", "disabled")

    def get_ext_mac(self, node_address: str) -> str:
        """Get the Extended MAC Address of a node at a given RLOC address.

        Args:
            node_address (str): the RLOC16 of the node.

        Raises:
            RuntimeError: If the Extended Address pattern is not found in the output.

        Returns:
            str: Decimal string representation of the parsed Extended MAC address.
        """
        raw = self.run_command("networkdiagnostic", "get", node_address, "0")
        m = re.search(r"Ext Address:\s*([0-9a-fA-F]+)", raw)
        if not m:
            msg = f"Could not find Ext Address in output:\n{raw}"
            raise RuntimeError(msg)
        mac_hex = m.group(1)
        return str(int(mac_hex, 16))

    def get_mesh_local_prefix(
        self,
    ) -> str:
        """Get the meshlocal prefix for the thread network.

        Raises:
            RuntimeError: when no prefix can be derived.

        Returns:
            str: the mesh local prefix.
        """
        out = self.run_command("dataset", "active")
        m = re.search(r"Mesh Local Prefix:\s*(\S+)", out)
        if not m:
            # We probably aren't connected to any thread network.
            msg = f"Could not find Mesh Local Prefix in dataset output:\n{out}"
            raise RuntimeError(msg)
        return m.group(1)

    def rloc16_to_address(self, mesh_local_prefix: str, rloc16: int) -> str:
        """Get an IPv6 address form the given mesh local prefix and RLOC.

        Args:
            mesh_local_prefix (str): The mesh local prefix from get_mesh_local_prefix.
            rloc16 (int): The thread device's RLOC16.

        Returns:
            str: The formed IPv6 Address.
        """
        prefix = mesh_local_prefix.split("/", maxsplit=1)[0].rstrip(":")
        return f"{prefix}:0:ff:fe00:{rloc16:x}"

    def _find_all_sleepy_children(self, mesh_local_prefix: str) -> list[dict]:
        """Parse `networkdiagnostic get <realm-local-all-routers> 1 16` output.

        Args:
            mesh_local_prefix (str): The mesh local prefix from get_mesh_local_prefix.

        Returns:
            list[dict]: A dictionary of SSEDs found.
        """
        raw = self.run_command(
            "networkdiagnostic", "get", LOCAL_MULTICAST_ADDR, "1", "16"
        )

        seds = []
        parent_rloc16 = None
        in_child_table = False
        current_entry = None

        # Parse the child table and extract the information for each child.
        for line in raw.splitlines():
            stripped = line.strip()

            if stripped.startswith("DIAG_GET.rsp/ans from"):
                parent_rloc16 = None
                in_child_table = False
                current_entry = None
                continue

            m = re.match(r"Rloc16:\s*0x([0-9a-fA-F]+)", stripped)
            if m:
                parent_rloc16 = int(m.group(1), 16)
                continue

            if stripped == "Child Table:":
                in_child_table = True
                continue

            if not in_child_table:
                continue

            m = re.match(r"-\s*ChildId:\s*0x([0-9a-fA-F]+)", stripped)
            if m:
                current_entry = {"child_id": int(m.group(1), 16)}
                continue

            if current_entry is None:
                continue

            m = re.match(r"RxOnWhenIdle:\s*(\d)", stripped)
            if m:
                current_entry["rx_on_when_idle"] = bool(int(m.group(1)))

                # RxOnWhenIdle is the last field we need per entry
                if parent_rloc16 is not None and not current_entry["rx_on_when_idle"]:
                    child_rloc16 = (parent_rloc16 & 0xFC00) | current_entry["child_id"]
                    seds.append(
                        {
                            "parent_rloc16": parent_rloc16,
                            "child_id": current_entry["child_id"],
                            "rloc16": child_rloc16,
                            "address": self.rloc16_to_address(
                                mesh_local_prefix, child_rloc16
                            ),
                        }
                    )

        return seds

    def _find_all_routers(self, mesh_local_prefix: str) -> list[dict]:
        raw = self.run_command("router", "table")
        routers = []

        for raw_line in raw.splitlines():
            line = raw_line.strip()
            if (
                not line.startswith("|")
                or "RLOC16" in line
                or set(line) <= {"|", "-", " "}
            ):
                continue
            cols = [c.strip() for c in line.strip("|").split("|")]

            num_expected_cols = 8
            if len(cols) < num_expected_cols:
                continue

            router_id = cols[0]
            rloc16_hex = cols[1]
            num_remaining_cols = 2
            ext_mac = cols[-2] if len(cols) >= num_remaining_cols else None

            rloc16 = int(rloc16_hex, 16)
            routers.append(
                {
                    "router_id": router_id,
                    "rloc16": rloc16,
                    "ext_mac": ext_mac,
                    "address": self.rloc16_to_address(mesh_local_prefix, rloc16),
                }
            )

        return routers

    def get_seds(self) -> list[dict]:
        """Get all the sleep end devices attached to the thread network.

        Returns:
            list[dict]: List of the parsed SEDs
        """
        ml_prefix = self.get_mesh_local_prefix()
        return self._find_all_sleepy_children(ml_prefix)

    def get_routers(self) -> list[dict]:
        """Get all the routers attached to the thread network.

        Returns:
            list[dict]: List of the parsed routers
        """
        ml_prefix = self.get_mesh_local_prefix()
        return self._find_all_routers(ml_prefix)
