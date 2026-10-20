#!/usr/bin/env python3
"""Check the effective, distro-patched source's asymmetric MTU contract."""
import hashlib
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1])
text = source.read_text()
# Public entry accepts TX first, RX second; GObex's constructor accepts RX first.
assert re.search(r"int\s+obex_session_start\(GIOChannel\s*\*io,\s*uint16_t\s+tx_mtu,\s*uint16_t\s+rx_mtu,", text), "unexpected session MTU signature"
start = text.index("int obex_session_start(")
body = text[start:text.index("\n}\n", start)]
assert re.search(r"g_obex_new\(io,\s*type,\s*rx_mtu,\s*tx_mtu\)", body), "RX/TX MTU ordering regression"
assert not re.search(r"g_obex_new\(io,\s*type,\s*tx_mtu,\s*rx_mtu\)", body), "reversed MTUs"
print(f"source SHA256: {hashlib.sha256(source.read_bytes()).hexdigest()} {source}")
print("PASS: effective distro source retains g_obex_new(io, type, rx_mtu, tx_mtu)")
