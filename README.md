# Reliable Warning Over Unreliable Infrastructure: A Consensus-Based IoT Flood Early-Warning System

This is the application repository for my CM3070 Final Project submission.

The code is split into sections:

- /firmware contains the Edge and Fog tier firmware along with the host side testing harness for raft and the unit tests.
- /gateway contains all the code run on the Raspeberry PI, from Chipstack Codec, to the Server and the WebUI for local and public dashboards.
- /integration-tests contain the integration test harness, which runs locally on the Raspberry PI to test the full system end-to-end.

Each folder has its own README.md which goes into further detail on that specific section.
