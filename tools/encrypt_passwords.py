#!/usr/bin/env python3
"""
XOR encrypt/decrypt a passwords.txt file for the Flipper Zero Pass Manager.

XOR encryption is symmetric: running the tool twice with the same PIN
restores the original file.

Usage:
  python3 encrypt_passwords.py <4-digit-pin> <input_file> [output_file]

  If output_file is omitted the result is written to stdout (binary).

Examples:
  # Encrypt plaintext -> put the result on the SD card:
  python3 encrypt_passwords.py 1234 passwords.txt passwords_enc.txt

  # Verify (decrypt back to stdout):
  python3 encrypt_passwords.py 1234 passwords_enc.txt
"""
import sys


def xor_crypt(data: bytes, pin: str) -> bytes:
    key = pin.encode("ascii")
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    pin = sys.argv[1]
    input_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else None

    if not pin.isdigit() or len(pin) != 4:
        print(
            f"Error: PIN must be exactly 4 decimal digits (got '{pin}')",
            file=sys.stderr,
        )
        sys.exit(1)

    with open(input_path, "rb") as fh:
        data = fh.read()

    result = xor_crypt(data, pin)

    if output_path:
        with open(output_path, "wb") as fh:
            fh.write(result)
        print(f"Done: {len(result)} bytes written to '{output_path}'")
    else:
        sys.stdout.buffer.write(result)


if __name__ == "__main__":
    main()
