# Flipper Zero Pass Manager

A minimal password manager for the Flipper Zero. It reads a plain-text file from the SD card, shows a scrollable list of entries, and can type credentials into any USB-connected host via BadUSB HID.

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
2. Connect the Flipper to a host via USB (the app handles mode switching automatically).
3. Open the target login form on the host and click the **username** field.
4. Launch **Pass Manager** from the Apps → Tools menu.
5. Scroll with **Up / Down**, select an entry with **OK**.
6. The detail screen shows the entry name, username, and password.
7. Press **OK** again to type: the Flipper switches to USB HID, waits ~1 s for
   host enumeration, then types `<username>` **Tab** `<password>`.
8. Press **Back** to return to the list; press **Back** again to exit.

### HID typing details

- The Flipper temporarily switches its USB mode to HID keyboard, types the
  credentials, then restores the previous USB mode.
- Supported characters: a–z, A–Z, 0–9, and the symbols
  `! @ # $ % ^ & * ( ) - _ = + [ { ] } \ | ; : ' " \` ~ , < . > / ?` and space.
- Unknown characters are silently skipped.

## Limits

| Constraint | Value |
|---|---|
| Max entries | 64 |
| Max field length | 63 chars |

## Security note

Passwords are stored in **plain text** on the SD card. This app is intended for convenience on a physically secured device, not as a cryptographically secure vault.
