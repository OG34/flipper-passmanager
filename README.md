# Flipper Zero Pass Manager

A password manager for the Flipper Zero. It reads a ChaCha20-encrypted
file from the SD card, unlocks it with a 6-digit PIN entered on the device,
shows a scrollable list of entries, and types credentials into any
USB-connected host via BadUSB HID.

## Password file

Create a plaintext file in this format:

```
name|username|password
```

Example (`passwords.txt`):

```
GitHub|myuser|s3cr3tpass
WiFi Home|admin|routerpass123
Email|john@example.com|emailpass!
SSH Server|root|topsecret
```

Encrypt it with the companion tool before copying to the SD card:

```sh
python3 tools/encrypt_passwords.py encrypt 123456 passwords.txt passwords_enc.txt
```

Copy the **encrypted** file to:

```
/ext/apps/Tools/passwords.txt
```

To verify decryption works before copying:

```sh
python3 tools/encrypt_passwords.py decrypt 123456 passwords_enc.txt
```

This prints the plaintext to stdout. If it matches your original file the
encryption is correct.

## Encryption

| Detail | Value |
|---|---|
| Algorithm | ChaCha20 (RFC 7539) |
| Key size | 32 bytes |
| Nonce | 12 bytes, random per encryption |
| Key derivation | PIN → 32-byte key via 10 000 mixing rounds |
| PIN length | 6 decimal digits (000000 – 999999) |
| File format | `[12-byte nonce][ChaCha20 ciphertext]` |

Each encryption run produces a different nonce, so the same plaintext
never produces the same ciphertext. The derived key and decrypted buffer
are zeroed in memory immediately after use. Entries are also wiped when
the user returns to the PIN screen or the auto-lock timer fires.

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

1. Encrypt `passwords.txt` with your 6-digit PIN and copy it to the SD card (see above).
2. Connect the Flipper to a host via USB (the app handles HID mode switching).
3. Open the target login form on the host and click the **username** field.
4. Launch **Pass Manager** from the Apps → Tools menu.
5. On the **PIN screen**: use **Up / Down** to change the highlighted digit,
   **Left / Right** to move between digits, **OK** to confirm.
   - Wrong PIN → error message appears on the PIN screen, try again.
6. If the PIN is correct the entry list appears. Scroll with **Up / Down**,
   select with **OK**.
7. The detail screen shows the entry name and username. The password is
   displayed as `********` to prevent shoulder-surfing.
8. Press **OK** to type: the Flipper switches to USB HID, waits ~1 s for
   host enumeration, then types `<username>` **Tab** `<password>`.
9. Press **Back** to return to the list; press **Back** again to return to
   the PIN screen (entries are wiped from RAM).

> **Auto-lock:** if no entry is typed for 60 seconds the app automatically
> wipes entries from RAM and returns to the PIN screen.

### HID typing details

- The Flipper temporarily switches USB mode to HID keyboard, types the
  credentials, then restores the previous USB mode.
- Supported characters: a–z, A–Z, 0–9, and
  `! @ # $ % ^ & * ( ) - _ = + [ { ] } \ | ; : ' " ` ~ , < . > / ?` and space.
- Unknown characters are silently skipped.

## Repository layout

```
passmanager/
  passmanager.c       # Application source (ChaCha20 + BadUSB)
  application.fam     # Flipper build manifest
tools/
  encrypt_passwords.py  # Pure-Python companion tool (no external deps)
passwords.txt.example   # Plaintext example — DO NOT copy this to the Flipper
```

## Limits

| Constraint | Value |
|---|---|
| PIN length | 6 digits (000000 – 999999) |
| Max entries | 64 |
| Max field length | 63 chars |
| Max file size | 12 400 bytes |

## Security note

ChaCha20 with a 6-digit PIN provides strong stream-cipher confidentiality for
the file at rest. The 10 000-round key derivation raises the cost of
brute-forcing all 1 000 000 PIN combinations.

Typical threat model: someone physically finds your SD card and cannot read
the credential file without your PIN. This does **not** protect against
keyloggers on the host machine or an attacker who has already obtained your PIN.
