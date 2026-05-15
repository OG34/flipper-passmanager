# Flipper Zero Pass Manager

A minimal password viewer for the Flipper Zero. It reads a plain-text file from the SD card and shows a scrollable list; selecting an entry reveals its username and password.

## Password file

Copy your passwords to:

```
/ext/apps/Tools/passwords.txt
```

One entry per line, pipe-separated:

```
name|username|password
```

Example:

```
GitHub|myuser|s3cr3tpass
WiFi Home|admin|routerpass123
Email|john@example.com|emailpass!
```

## Build

Requires the [Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware) SDK.

```sh
./fbt fap_passmanager
```

Or build and deploy directly to a connected Flipper:

```sh
./fbt launch_app APPSRC=applications_user/passmanager
```

Copy the compiled `.fap` to `/ext/apps/Tools/` on the SD card.

## Usage

1. Put `passwords.txt` on the SD card at the path above.
2. Launch **Pass Manager** from the Apps → Tools menu.
3. Scroll with **Up / Down**, select an entry with **OK**.
4. The detail screen shows the username and password.
5. Press **Back** to return to the list, press **Back** again to exit.

## Limits

| Constraint | Value |
|---|---|
| Max entries | 64 |
| Max field length | 63 chars |

## Security note

Passwords are stored in **plain text** on the SD card. This app is intended for convenience on a physically secured device, not as a cryptographically secure vault.
