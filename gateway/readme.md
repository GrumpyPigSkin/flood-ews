# Gateway Folder #

## chirpstack-codec ##

Contains the codec Chirpstack uses for parsing LoRaWAN messages sent from the
fog layer. Chirpstack runs on the RPi and takes care of communicating with the
SX-1302 hardware, and running the uplink back from Chirpstack to the server.

## dashboard ##

Contains the UI for the Local and Cloud versions of the dashboard.

The local version runs on the RPi itself and can be connected to over LAN allows
full access to all services such as:
- Reading sensor data.
- Reading and overriding actuators.
- Configuring external sources to work with this EWS.
- Configuring egress targets to send data collected from the station to.

The Cloud version is deployed to Cloudflare pages and connect to a SupaBase
instance. Not directly to the RPi gateway. This offers just a read endpoint to
read the latest values sent from the server.

Cloud location: [dashboard](https://a3a7b780.flood-ews-dashboard.pages.dev/)

## nginx ##

Contains the configuration file for Nginx running on the RPi. This serves the
static dashboard files and proxies to the Server API.

## server ##

Contains the Go server, this runs continually in the background for messages
from Chirpstack and polls external sources for new data. It is also responsible
for taking local action for example closing an actuator on an alert. It also
handles the WebSocket connection to the local UI, egress targets which are
external targets we send data to. The main aspect of this is we pull and push
all data here, no one connects to us to push data to us.

## services ##

Contains the system services running on the RPi which handles automating the
startup of:
- Chripstack Concentratord: The service that handles the SX1302 hardware.
- Chirstack Docker: Handles chirpstack configuration.
- ChirpStack MQTT Forwarder: Handles forwarding messages over MQTT.
- Flood EWS Sever: Starting up and running the Go web server.
