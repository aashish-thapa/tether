#!/usr/bin/env bash
# Run only inside a disposable Ubuntu container, never on the host.
set -euo pipefail
: "${EXPECTED_OBEX_VERSION:?Supply the tested runtime package version}"
python3 - <<'PY'
from pathlib import Path
p = Path('/etc/apt/sources.list.d/ubuntu.sources')
p.write_text(p.read_text().replace('Types: deb\n', 'Types: deb deb-src\n'))
PY
apt-get update
apt-get install -y --no-install-recommends dpkg-dev ca-certificates
mkdir -p /evidence
cd /evidence
apt-get source "bluez=$EXPECTED_OBEX_VERSION"
source_dir=$(printf '%s\n' bluez-*/)
[[ $(dpkg-parsechangelog -l "$source_dir/debian/changelog" -S Version) == "$EXPECTED_OBEX_VERSION" ]]
[[ $EXPECTED_OBEX_VERSION == 5.85-* ]]
printf 'Authenticated Ubuntu source version: %s\n' "$EXPECTED_OBEX_VERSION"
printf 'Applied distro patches:\n'
cat "$source_dir/debian/patches/series"
python3 /src/scripts/test-obex-source.py "$source_dir/obexd/src/obex.c"
# A real-source mutation must fail for the intended assertion, not setup errors.
sed 's/g_obex_new(io, type, rx_mtu, tx_mtu)/g_obex_new(io, type, tx_mtu, rx_mtu)/' \
    "$source_dir/obexd/src/obex.c" > reversed-obex.c
set +e
python3 /src/scripts/test-obex-source.py reversed-obex.c > mutation.log 2>&1
status=$?
set -e
[[ $status == 1 ]]
grep -F 'RX/TX MTU ordering regression' mutation.log
printf 'PASS: source gate rejects reversed asymmetric RX/TX MTUs\n'
