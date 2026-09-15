# Server #

Runs the local web server on the RPI which, each folder handles a separate
function.

## Actuator ##

Handles actuation, this could be a flood barrier, siren, or some other form of
alarm. The interface for an actuator is common and must provide the `Apply`
function. The file itself handles:

- A watchdog, if the rest of the application goes quiet it will drive the
  actuators to their failsafe position.
- Override: An operator can manually override an actuator to a given position.

Actuators are accompanied by a spec which specifies valid states, whether it is
active, and the failsafe state.

## Advisory ##

We can poll other sources of data for example from a different EWS authority.
This input is then translated into a common advisory which is used alongside
sensor data to actuate outputs or give warnings. Advisories can also be operator
approved where it is pushed to the console and a human has to intervene to act on it.

## Api ##

This handles the Web API. Almost all endpoints need a form of authentication
which is handled through JWT.

## Egress ##

Egress allows the configuration of external targets for us to push data to. When
new data is received we can push this data to the egress location. There are two
types of egress targets:

- Supabase: which require a DSN and a database setup to receive data.
- Webhook: Which could be a neighbouring authority or governing body we need to
  tell about alerts, water levels etc.

## Policy ##

Policies are the layer between advisories and actuation. It receives an advisory
and then has to decide what action does it take. Either:

- Directly take action and drive an actuator.
- Queue the action to be approved by an operator.

## Poller ##

Poller polls external sources for data, matches them to a policy and provides
the action. It wakes a goroutine on each interval to request new data from the
source.

A simple validator filled in by the operator tells the system what data to
listen to in the returned value and how that maps to the internal advisory. This
allows for mapping of values to our internal warning severity level.

## Store ##

Handles the SQLite database which stores the Policies, advisories, egress
targets, actuator data and an audit log.

I just define the schema, the SQLC generates the Go code for access and
manipulating the database based on my queries and schema

## Telemetry ##

This listens to Data from Chirpstacks MQTT forwarder. This then serves the data
to the upstream UI websocket, handles replays when connected, and sends the data
onwards to supabase or external webhooks.

## Building and Testing ##

The software is built and tested using go version:

```bash
go version go1.26.4
```

Go must be installed beforehand: https://go.dev/doc/install

### Build and Run ###

Run:

```bash
# Build
go build -o flood-ews_server.exe main.go

# Run
./flood-ews_server
```

### Testing ###

To run the unit tests, run this from the same folder as `main.go`

```bash
go test ./...
?       server  [no test files]
ok      server/actuator (cached)
ok      server/advisory (cached)
ok      server/api      1.821s
?       server/egress   [no test files]
ok      server/policy   (cached)
ok      server/poller   (cached)
ok      server/store    (cached)
?       server/telemetry        [no test files]
```
