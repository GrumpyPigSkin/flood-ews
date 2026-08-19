# Integration Tests for the full system

These tests test the whole system end to end. They work by injecting faults and
monitoring outputs into the system. Faults are injected over two means:

- JLink: The JLink ports control the CPU and enable resetting on Fog nodes for certain tests.
- CoAP: The nRF52840 Dongle allows the test software to write CoAP messages to the sensors and fog layer as a back door.

## Running the tests

These tests are not easy to run and require a special build to compile the
firmware with the fault logic. These tests also need to be run from the Raspberry
PI running the gateway software.

To enable the fault logic compile the firmware with the definition
`-DCONFIG_ENABLE_FAULT_INJECTION` this will enable the CoAP endpoint and log messages
to run the test.

### Hardware required

[nRF52840 Dongle](https://www.nordicsemi.com/Products/Development-hardware/nRF52840-Dongle):
This runs the spinel software that allows the CoAP messages to be sent to the
thread nodes.

You need to follow the instructions here: [Openthread Spinel](https://openthread.io/codelabs/openthread-hardware#2)
to build the image for the USB dongle and the Application for Linux.
The difference here is you need to use the DFU programmer included with nrf-connect.

Build the latest version of the Linux software on the Raspberry PI and you will end up with two applications:

`ot-daemon`: Connects to the USB dongle.
`ot-ctl`: Used by the tests to connect to the thread network.

Running the daemon:

`sudo ./build/posix/src/posix/ot-daemon -v 'spinel+hdlc+uart:///dev/ttyACM0?uart-baudrate=1000000'`

Note: ttyACM0 may be different on your system. If you see any errors make sure
openthread spinel version matches the software version flashed to the USB. Build everything from: [NRF OT Repo](https://github.com/openthread/ot-nrf528xx.git)

Get `ot-ctl` to connect to the thread network, this requires the system to be up
first and thread is healthy:

```bash
sudo ./build/posix/src/posix/ot-ctl
dataset networkkey 00112233445566778899AABBCCDDEEFF
dataset channel 15
dataset panid 0x1234
dataset extpanid 1111111122222222
dataset commit active
ifconfig up
thread start
state # should print child when connected.
```

Note: Edit with you credentials if they differ.

### Software versions required

```bash
python --version
Python 3.14.0
```

## Running the tests on the PI

Copy the `integration-tests` folder to the Raspberry Pi.

Create a `.venv` and install packages from `requirements.txt`.

Edit `config.py` to change any endpoints, passwords etc if needed.
