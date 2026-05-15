#!/usr/bin/env python3
"""
Encrypt / decrypt a passwords.txt file for the Flipper Zero Pass Manager.

Uses ChaCha20 (RFC 7539) with a PIN-derived 32-byte key.
No external dependencies — pure Python 3.6+.

File format:  [12-byte random nonce]  [ChaCha20 ciphertext]

Usage:
  Encrypt:  python3 encrypt_passwords.py encrypt <6-digit-pin> <plaintext.txt> <output>
  Decrypt:  python3 encrypt_passwords.py decrypt <6-digit-pin> <encrypted>    [output]
            (omit output to print plaintext to stdout)

Examples:
  python3 encrypt_passwords.py encrypt 123456 passwords.txt passwords_enc.txt
  python3 encrypt_passwords.py decrypt 123456 passwords_enc.txt
"""
import os
import struct
import sys

# ── ChaCha20 (RFC 7539) ──────────────────────────────────────────────────────

def _rotl32(v: int, n: int) -> int:
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF


def _qr(s: list, a: int, b: int, c: int, d: int) -> None:
    s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] ^= s[a]; s[d] = _rotl32(s[d], 16)
    s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] ^= s[c]; s[b] = _rotl32(s[b], 12)
    s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] ^= s[a]; s[d] = _rotl32(s[d],  8)
    s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] ^= s[c]; s[b] = _rotl32(s[b],  7)


def _chacha20_block(key: bytes, counter: int, nonce: bytes) -> bytes:
    C = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574]
    k = list(struct.unpack_from("<8I", key))
    n = list(struct.unpack_from("<3I", nonce))
    s = C + k + [counter & 0xFFFFFFFF] + n
    w = s[:]
    for _ in range(10):
        _qr(w, 0, 4,  8, 12); _qr(w, 1, 5,  9, 13)
        _qr(w, 2, 6, 10, 14); _qr(w, 3, 7, 11, 15)
        _qr(w, 0, 5, 10, 15); _qr(w, 1, 6, 11, 12)
        _qr(w, 2, 7,  8, 13); _qr(w, 3, 4,  9, 14)
    return struct.pack("<16I", *[(w[i] + s[i]) & 0xFFFFFFFF for i in range(16)])


def _chacha20(key: bytes, nonce: bytes, data: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(data), 64):
        stream = _chacha20_block(key, i // 64, nonce)
        block  = data[i : i + 64]
        out.extend(a ^ b for a, b in zip(block, stream))
    return bytes(out)


# ── Key derivation (must match C implementation exactly) ─────────────────────

def _derive_key(pin: str) -> bytes:
    """Stretch a numeric PIN into a 32-byte ChaCha20 key."""
    pin_b = pin.encode("ascii")
    key   = bytearray(pin_b[i % len(pin_b)] for i in range(32))
    for r in range(10_000):
        carry = 0
        for i in range(32):
            v      = key[i] + key[(i + 1) % 32] + carry + pin_b[r % len(pin_b)]
            key[i] = v & 0xFF
            carry  = v >> 8
    return bytes(key)


# ── Encrypt / decrypt ────────────────────────────────────────────────────────

def encrypt(pin: str, plaintext: bytes) -> bytes:
    nonce = os.urandom(12)
    key   = _derive_key(pin)
    return nonce + _chacha20(key, nonce, plaintext)


def decrypt(pin: str, data: bytes) -> bytes:
    if len(data) <= 12:
        raise ValueError("File too short — not a valid encrypted file.")
    nonce, ciphertext = data[:12], data[12:]
    key = _derive_key(pin)
    return _chacha20(key, nonce, ciphertext)


# ── CLI ──────────────────────────────────────────────────────────────────────

def main() -> None:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    action     = sys.argv[1].lower()
    pin        = sys.argv[2]
    input_path = sys.argv[3]
    output_path = sys.argv[4] if len(sys.argv) > 4 else None

    if action not in ("encrypt", "decrypt"):
        print(f"Error: action must be 'encrypt' or 'decrypt' (got '{action}')", file=sys.stderr)
        sys.exit(1)

    if not pin.isdigit() or len(pin) != 6:
        print(f"Error: PIN must be exactly 6 decimal digits (got '{pin}')", file=sys.stderr)
        sys.exit(1)

    with open(input_path, "rb") as fh:
        raw = fh.read()

    try:
        result = encrypt(pin, raw) if action == "encrypt" else decrypt(pin, raw)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    if output_path:
        with open(output_path, "wb") as fh:
            fh.write(result)
        verb = "Encrypted" if action == "encrypt" else "Decrypted"
        print(f"{verb}: {len(result)} bytes written to '{output_path}'")
    else:
        sys.stdout.buffer.write(result)


if __name__ == "__main__":
    main()
