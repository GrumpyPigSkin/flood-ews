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
            f"{self.base_url}/v1/login",
            json={"password": password},
        )
        response.raise_for_status()
        token = response.json()["token"]
        self.session.headers.update({"Authorization": f"Bearer {token}"})
        return token

    def list_actuators(self) -> list[dict]:
        """Get the list of actuators.

        Returns:
            _type_: The list of actuators.
        """
        response = self.session.get(f"{self.base_url}/v1/actuators")
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
