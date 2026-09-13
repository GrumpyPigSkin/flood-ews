"""GatewayApiClient handles communicating with the go gateway."""
import requests


class GatewayApiClient:
    """GatewayApiClient handles communicating with the go gateway."""

    def __init__(self, base_url: str) -> None:
        """Initialise ApiClient.

        Args:
            base_url (str): The URL of the gateway server.
        """
        self.base_url = base_url
        self.session = requests.Session()

    def login(self, password: str) -> str:
        """Login to the gateway to access read endpoints.

        Args:
            password (str): The password to login.

        Returns:
            str: The token.
        """
        response = self.session.post(
            f"{self.base_url}/v1/auth/login",
            json={"password": password},
        )
        response.raise_for_status()
        token = response.json()["token"]
        self.session.headers.update({"Authorization": f"Bearer {token}"})
        return token

    def list_actuators(self) -> list[dict]:
        """Get the list of actuators.

        Returns:
            list[dict]: The list of actuators.
        """
        response = self.session.get(f"{self.base_url}/v1/config/actuators")
        response.raise_for_status()
        return response.json()

    def get_actuator(self, actuator_id: str) -> dict | None:
        """Get a specific actuator.

        Args:
            actuator_id (str): The actuator to find.

        Returns:
            dict | None: the actuator if found or None.
        """
        actuators = self.list_actuators()
        for actuator in actuators:
            if actuator["id"] == actuator_id:
                return actuator
        return None

    def get_actuator_state(self, actuator_id: str) -> str | None:
        """Get the list of actuators.

        Returns:
            str | none : The actuator state or None.
        """
        response = self.session.get(f"{self.base_url}/v1/dashboard/actuators")
        response.raise_for_status()

        actuators = response.json()

        if actuators[actuator_id] is not None:
            return actuators[actuator_id]

        return None
