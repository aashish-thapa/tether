#!/usr/bin/env bash
# Package boundary: reject distro OBEX releases predating the RX/TX MTU fix.
set -euo pipefail
version=$(dpkg-query -W -f='${Version}' bluez-obexd)
printf 'bluez-obexd package version: %s\n' "$version"
if ! dpkg --compare-versions "$version" ge 5.84; then
    printf 'FAIL: bluez-obexd must be >= 5.84 (RX/TX MTU fix)\n' >&2
    exit 42
fi
binary=$(dpkg -L bluez-obexd | grep '/obexd$')
test -x "$binary"
dpkg-query -S "$binary"
"$binary" --help | grep -F -- '--nodetach'
sha256sum "$binary"
if [[ -L /usr/local/libexec/tether-obexd ]]; then
    [[ $(readlink -f /usr/local/libexec/tether-obexd) == "$binary" ]]
    test ! -e /usr/share/dbus-1/services/org.bluez.obex.service
    [[ $(stat -c '%u:%a' /run/systemd) == '0:700' ]]
    test ! -r /run/systemd
    printf 'PASS: non-root headless runtime cannot inherit logind seats\n'
fi
printf 'PASS: packaged OBEX version and executable provenance\n'
