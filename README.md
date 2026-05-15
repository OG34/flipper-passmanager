# Flipper Zero Pass Manager

A minimal password manager for the Flipper Zero. It reads a XOR-encrypted
file from the SD card, unlocks it with a 4-digit PIN entered on the device,
shows a scrollable list of entries, and can type credentials into any
USB-connected host via BadUSB HID.

## Password file

The file must be XOR-encrypted with your PIN before copying to the SD card.
Create a plaintext file first:

```
name|username|password
```

Example (`passwords.txt`):

```
GitHub|myuser|s3cr3tpass
WiFi Home|admin|routerpass123
Email|john@example.com|emailpass!
```

Then encrypt it with the companion tool:

```sh
python3 tools/encrypt_passwords.py 1234 passwords.txt passwords_enc.txt
```

Copy the **encrypted** file to:

```
/ext/apps/Tools/passwords.txt
```

To verify the encrypted file decrypts correctly, run the same command again
(XOR is symmetric — applying it twice with the same PIN restores the original):

```sh
python3 tools/encrypt_passwords.py 1234 passwords_enc.txt
```

## Encryption

The file is XOR-encrypted with the PIN string used as a repeating key.

| Detail | Value |
|---|---|
| Algorithm | XOR (symmetric stream cipher) |
| Key | 4-digit PIN as ASCII bytes (`"1234"` → `0x31 0x32 0x33 0x34`) |
| Key length | 4 bytes, repeating |

The decrypted buffer is zeroed in memory before being freed. XOR with a
short PIN is lightweight and deters casual SD-card inspection; it is **not**
cryptographically strong — do not rely on it as your only security layer.

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

1. Encrypt `passwords.txt` with your PIN and copy it to the SD card (see above).
2. Connect the Flipper to a host via USB (the app handles HID mode switching).
3. Open the target login form on the host and click the **username** field.
4. Launch **Pass Manager** from the Apps → Tools menu.
5. On the **PIN screen**: use **Up / Down** to change the highlighted digit,
   **Left / Right** to move between digits, **OK** to confirm.
6. If the PIN is correct the entry list appears. Scroll with **Up / Down**,
   select with **OK**.
7. The detail screen shows the entry name, username, and password.
8. Press **OK** to type: the Flipper switches to USB HID, waits ~1 s for
   host enumeration, then types `<username>` **Tab** `<password>`.
9. Press **Back** to return to the list; press **Back** again to exit.

> If "No entries / wrong PIN" appears, the PIN was incorrect or the file is
> missing. Press **Back** and try again.

### HID typing details

- The Flipper temporarily switches USB mode to HID keyboard, types the
  credentials, then restores the previous USB mode.
- Supported characters: a–z, A–Z, 0–9, and
  `! @ # $ % ^ & * ( ) - _ = + [ { ] } \ | ; : ' " ` ~ , < . > / ?` and space.
- Unknown characters are silently skipped.

## Limits

| Constraint | Value |
|---|---|
| PIN length | 4 digits (0000 – 9999) |
| Max entries | 64 |
| Max field length | 63 chars |
| Max file size | 12 400 bytes |

## Security note

XOR with a short PIN is a lightweight obfuscation layer, not strong
encryption. For high-value credentials, use full-disk encryption on the SD
card or store only the credentials you are comfortable losing.
