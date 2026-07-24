# Dashboard #

Dashboard is UI that displays either the public dashboard, which is a read only screen for the public to check on conditions. And the operator console, which allows for configuration of the gateway, deep-dive screens, and actuator overrides.

## Design ##

The design of the UI follows MVVM principles:

### lib/config ###

Contains application configuration.
- Cloud config: only require the "cloud" flag, Supabase URL and public key at
  build. This then prevents any of the other screens being built which allows
  maximum security.
- Local mode: Gates all actions behind a login screen that only operators can
  access with a password.

### lib/model ###

These are the data structures that are sent to/from the API.

### lib/previews ###

Previews allow for testing of screens without needing the whole application.
Each screen or widget is fed with fake data, so it can be previewed. More
information on this in testing.

### lib/screens ###

These are the main screens for the application. In the cloud build only the
dashboard screen is used. In the local build all screens are used.

### lib/services ###

This contains all the View Model elements. Such as controlling access to the local server or supabase, handling form validation and authentication, handling the websocket connection.

### lib/widgets ###

These are small reusable UI components used to achieve a consistent look and
feel across the application and prevent code duplication.

## Build and Testing ##

To build and test the application it is recommended that you install the Flutter developer tools in VSCode, after which you will be prompted to install the latest version of Flutter. The version of flutter I used to build the UI is:

```bash
PS E:\Uni\final-project\flood-ews\gateway\dashboard> flutter --version
Flutter 3.44.6 • channel stable • https://github.com/flutter/flutter.git
Framework • revision ee80f08bbf (2 weeks ago) • 2026-07-08 15:02:06 -0700
Engine • hash d3a3293399556a85388faf8c6f0723a7a5597aa8 (revision 83675ed276) (23 days ago) • 2026-06-30 16:59:03.000Z
Tools • Dart 3.12.2 • DevTools 2.57.0
```

### Building ###

To build the two versions I rely on external environment files that are used to compile the application.

#### Local Build ####

The definitions JSON `local.json` file:

```JSON
{
  "ROLE": "local"
}
```

Then build the application:

```bash
flutter build web --release --dart-define-from-file local.json
```

The output will be in `/build/web` and can be deployed to the RPi.

#### Cloud Build ####

The definitions JSON `cloud.json` file:

```JSON
{
  "ROLE": "cloud",
  "SUPABASE_URL": "<the url>",
  "SUPABASE_KEY": "<the publishable key>"
}

```

Then build the application:

```bash
flutter build web --release --dart-define-from-file cloud.json
```

The output will be in `/build/web` and can be deployed to the cloud.

### Testing ###

To test the application I relied on using previews. To the run the previews from the route of dashboard folder run:

```bash
flutter widget-preview start
```

This then launches a browser and the UI should be shown in there. A note on the
previewer is that it is fairly temperamental. You might have better luck running
the VSCode Widget Previewer included with the flutter SDK, but you will need to
make the pain rather large.
