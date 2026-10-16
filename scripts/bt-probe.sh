#!/usr/bin/env bash
# Report whether this machine can carry Tether's iPhone Bluetooth features.
#
# Full mode      = MAP + PBAP (messages, contacts) + ANCS (notifications)
# Compatibility  = MAP + PBAP only; no notification mirroring
#
# Read-only unless --set-class is given.
#
#   --set-class   set the adapter's Class of Device to A/V Hands-Free
#   --calls       probe the call path; run it while a call is connected

set -uo pipefail

ADAPTER=${ADAPTER:-hci0}
ADAPTER_PATH="/org/bluez/$ADAPTER"

# A/V Hands-Free. iOS only offers the Messages/Contacts permission toggles to a
# device presenting this class. BlueZ ORs service bits on top, so only the low
# 13 bits (format + major + minor) are comparable.
TARGET_COD_LOW=0x0408
COD_MASK=0x1fff

set_class=0
calls_only=0
case ${1:-} in
    --set-class) set_class=1 ;;
    --calls) calls_only=1 ;;
esac

red=$'\e[31m'; grn=$'\e[32m'; ylw=$'\e[33m'; dim=$'\e[2m'; rst=$'\e[0m'
[[ -t 1 ]] || { red=; grn=; ylw=; dim=; rst=; }

blockers=0    # hardware/stack cannot do the job at all
degraded=0    # capable of MAP/PBAP but not ANCS
conflicts=0   # capable, but something else is in the way right now

ok()    { printf '  %s✓%s %s\n' "$grn" "$rst" "$1"; }
warn()  { printf '  %s!%s %s\n' "$ylw" "$rst" "$1"; degraded=$((degraded + 1)); }
clash() { printf '  %s!%s %s\n' "$ylw" "$rst" "$1"; conflicts=$((conflicts + 1)); }
bad()   { printf '  %s✗%s %s\n' "$red" "$rst" "$1"; blockers=$((blockers + 1)); }
note()  { printf '    %s%s%s\n' "$dim" "$1" "$rst"; }

prop() {
    busctl --system get-property org.bluez "$ADAPTER_PATH" "$1" "$2" 2>/dev/null |
        awk '{ $1=""; sub(/^ /,""); gsub(/"/,""); print }'
}

# Where the call audio goes is decided by whoever owns the hands-free profile,
# and only an active call proves it. Run this while a call is connected.
probe_calls() {
    echo
    echo "Call audio probe (run this while a call is CONNECTED)"
    echo "====================================================="

    echo
    echo "BlueZ telephony objects"
    local tree
    tree=$(busctl --system tree org.bluez 2>/dev/null)
    if grep -q '/telephony' <<<"$tree"; then
        grep -o '/org/bluez/[^ ]*/telephony[0-9]*[^ ]*' <<<"$tree" | sort -u | while read -r path; do
            ok "$path"
            # Only the telephony interface; the generic D-Bus ones are noise.
            busctl --system introspect org.bluez "$path" 2>/dev/null |
                awk '/^org\.bluez\.(Telephony|Call)1/ { p = 1; next }
                     /^org\./ { p = 0 }
                     p && /^\./ { printf "      %s\n", $0 }'
        done
    else
        bad "no telephony object — HFP is not connected, or bluetoothd lacks --experimental"
    fi

    # The whole question: does BlueZ hand the SCO link to an audio server?
    echo
    echo "Media transports (a transport here would mean the audio can reach the desktop)"
    if grep -qE '/fd[0-9]+' <<<"$tree"; then
        grep -oE '/org/bluez/[^ ]*/fd[0-9]+' <<<"$tree" | sort -u | while read -r path; do
            clash "$path"
            busctl --system get-property org.bluez "$path" org.bluez.MediaTransport1 UUID 2>/dev/null |
                awk '{ printf "      UUID %s\n", $2 }'
        done
        note "Unexpected: report this, it changes what Tether can do."
    else
        ok "none — BlueZ exports no transport, so the audio stays on the phone"
    fi

    echo
    echo "Audio server view"
    if command -v pactl >/dev/null; then
        if pactl list cards short 2>/dev/null | grep -q bluez; then
            clash "a bluez_card exists:"
            pactl list cards short 2>/dev/null | grep bluez | awk '{ printf "      %s\n", $2 }'
        else
            ok "no bluez_card — nothing for the desktop to play through"
        fi
    else
        note "pactl not installed; skipping"
    fi

    echo
    echo "bluetoothd, last 40 lines"
    journalctl -u bluetooth -n 40 --no-pager 2>/dev/null |
        grep -iE 'sco|transport|codec|hfp' | tail -10 |
        awk '{ printf "    %s\n", $0 }' || note "no matching log lines"

    echo
    echo "Verdict"
    if grep -qE '/fd[0-9]+' <<<"$tree"; then
        printf '  %sTRANSPORT PRESENT%s — the call audio can reach this machine.\n' "$ylw" "$rst"
    else
        printf '  %sCONTROL ONLY%s — BlueZ signals the call but never opens the voice link.\n' "$grn" "$rst"
        printf '  The audio plays on the phone. See "Calls" in docs/BLUETOOTH.md.\n'
    fi
    exit 0
}

(( calls_only == 1 )) && probe_calls

echo
echo "Tether Bluetooth capability probe"
echo "================================="

# --- tools ---------------------------------------------------------------
echo
echo "Tooling"
for t in busctl btmgmt bluetoothctl; do
    if command -v "$t" >/dev/null; then ok "$t"; else bad "$t not found"; fi
done
command -v btmon >/dev/null || note "btmon not installed; useful for debugging pairing"

# --- bluez ---------------------------------------------------------------
echo
echo "BlueZ"
bluez_ver=$(bluetoothctl --version 2>/dev/null | awk '{print $2}')
if [[ -z $bluez_ver ]]; then
    bad "could not determine BlueZ version"
elif [[ $(printf '%s\n5.86\n' "$bluez_ver" | sort -V | head -1) == "5.86" ]]; then
    ok "version $bluez_ver (>= 5.86)"
else
    warn "version $bluez_ver is older than 5.86 — ANCS unavailable"
    note "MAP and PBAP still work; notification mirroring does not"
fi

# org.bluez.Bearer.LE1 appears only on bonded devices, and only when bluetoothd
# runs with --experimental (short form -E). A live interface on an existing bond
# is ground truth; the daemon command line is the fallback before anything is
# paired. Do not test for the long form alone, Arch's packaging uses -E.
bearer_seen=$(busctl --system call org.bluez / org.freedesktop.DBus.ObjectManager \
    GetManagedObjects 2>/dev/null | grep -c 'org\.bluez\.Bearer\.LE1')
if (( bearer_seen > 0 )); then
    ok "Bearer.LE1 present on a bonded device (Bearer API confirmed live)"
elif pgrep -af 'bluetoothd' | grep -qE -- '(^|[[:space:]])(-E|--experimental)([[:space:]]|$)'; then
    ok "bluetoothd running with --experimental (Bearer API available)"
else
    warn "bluetoothd is NOT running with --experimental — Bearer.LE1 unavailable"
    note "ANCS needs it to drive the LE half of the bond. To enable:"
    note "  sudo mkdir -p /etc/systemd/system/bluetooth.service.d"
    note "  printf '[Service]\\nExecStart=\\nExecStart=/usr/lib/bluetooth/bluetoothd --experimental\\n' \\"
    note "    | sudo tee /etc/systemd/system/bluetooth.service.d/experimental.conf"
    note "  sudo systemctl daemon-reload && sudo systemctl restart bluetooth"
fi

# --- adapter -------------------------------------------------------------
echo
echo "Adapter ($ADAPTER)"
if ! busctl --system introspect org.bluez "$ADAPTER_PATH" >/dev/null 2>&1; then
    bad "adapter $ADAPTER not present on the system bus"
    echo
    echo "Set ADAPTER=hciN to probe a different adapter."
    exit 1
fi

addr=$(prop org.bluez.Adapter1 Address)
name=$(prop org.bluez.Adapter1 Alias)
ok "$addr ($name)"

# The chip, which BlueZ never reports: Adapter1.Modalias is BlueZ's own device id
# and reads the same on every machine. Bug reports need the hardware named.
controller=$(cat "/sys/class/bluetooth/$ADAPTER/device/modalias" 2>/dev/null || true)
[[ -n $controller ]] && note "controller $controller"

[[ $(prop org.bluez.Adapter1 Powered) == true ]] && ok "powered on" || bad "powered off (bluetoothctl power on)"

roles=$(prop org.bluez.Adapter1 Roles)
# BR/EDR is implied by a working Adapter1; LE requires both GATT roles. Peripheral
# is what lets us broadcast the post-bond ANCS solicitation advert that makes iOS
# surface its permission toggles.
[[ $roles == *central* ]]    && ok "LE central role"    || bad "no LE central role — ANCS impossible"
[[ $roles == *peripheral* ]] && ok "LE peripheral role" || warn "no LE peripheral role — cannot solicit ANCS"

instances=$(prop org.bluez.LEAdvertisingManager1 SupportedInstances)
if [[ -n ${instances:-} && $instances -gt 0 ]]; then
    ok "advertising supported ($instances instances)"
else
    warn "no advertising instances — iOS may never show the permission toggles"
fi

# --- class of device -----------------------------------------------------
echo
echo "Class of Device"
cod=$(prop org.bluez.Adapter1 Class)
if [[ -z ${cod:-} ]]; then
    warn "could not read adapter class"
else
    printf -v cod_hex '0x%06x' "$cod"
    low=$(( cod & COD_MASK ))
    if (( low == TARGET_COD_LOW )); then
        ok "$cod_hex — A/V Hands-Free (major 4, minor 8)"
    else
        major=$(( (cod >> 8) & 0x1f ))
        warn "$cod_hex — major $major, expected A/V Hands-Free (major 4, minor 8)"
        note "iOS will not treat this machine as an eligible accessory."
        if (( set_class )); then
            echo
            note "running: btmgmt class 4 8"
            if echo | sudo btmgmt --index "${ADAPTER#hci}" class 4 8; then
                note "class set; re-run this probe to confirm"
                note "Make it stick: sudo systemctl enable --now tether-btclass@$ADAPTER"
            else
                note "failed — run 'sudo btmgmt class 4 8' by hand"
            fi
        else
            note "Fix with: $0 --set-class   (or: sudo btmgmt class 4 8)"
            note "That lasts until bluetoothd restarts. To make it stick:"
            note "  sudo systemctl enable --now tether-btclass@$ADAPTER"
        fi
    fi
fi

# --- bonded devices ------------------------------------------------------
# A bond pinned to PreferredBearer=bredr refuses the inbound LE link the iPhone
# opens in answer to the ANCS solicitation, so notifications never arrive while
# messages and contacts look perfect.
echo
echo "Bonded devices"

dev_paths=$(busctl --system tree org.bluez 2>/dev/null |
    grep -o "$ADAPTER_PATH/dev_[0-9A-F_]*$" | sort -u)

if [[ -z $dev_paths ]]; then
    note "none bonded on $ADAPTER"
else
    while read -r dev; do
        [[ -n $dev ]] || continue
        devprop() {
            busctl --system get-property org.bluez "$dev" "$1" "$2" 2>/dev/null |
                awk '{ $1=""; sub(/^ /,""); gsub(/"/,""); print }'
        }
        [[ $(devprop org.bluez.Device1 Paired) == true ]] || continue
        alias=$(devprop org.bluez.Device1 Alias)
        bearer=$(devprop org.bluez.Device1 PreferredBearer)
        le_conn=$(devprop org.bluez.Bearer.LE1 Connected)

        if [[ -z $bearer ]]; then
            note "$alias — PreferredBearer not exposed by this BlueZ"
        elif [[ $bearer == bredr && $le_conn != true ]]; then
            warn "$alias — PreferredBearer=bredr with LE down; the bond is refusing the LE link"
            note "Clear it: busctl set-property org.bluez $dev org.bluez.Device1 PreferredBearer s le"
        else
            ok "$alias — PreferredBearer=$bearer"
        fi
    done <<<"$dev_paths"
fi

# --- conflicts -----------------------------------------------------------
# The iPhone serves exactly one MAP session at a time. Any other local process
# holding it makes Tether's MAP connect fail with "Connection refused (111)",
# which looks nothing like a permissions problem and wastes a lot of debugging.
echo
echo "Conflicts"

if systemctl --user is-active --quiet obex.service 2>/dev/null; then
    ok "obexd active (required for MAP and PBAP)"
else
    warn "obexd is not active — MAP and PBAP will not connect"
    note "It is socket-activated; 'systemctl --user start obex' forces it up."
fi

# obexd opens MAP and PBAP over L2CAP in ERTM mode; with ERTM disabled the
# kernel refuses it and obexd reports "Unable to find service record".
if [[ $(cat /sys/module/bluetooth/parameters/disable_ertm 2>/dev/null) == [Y1] ]]; then
    warn "kernel has L2CAP ERTM disabled — MAP and PBAP will fail with no_record"
    note "Fix for this boot: echo N | sudo tee /sys/module/bluetooth/parameters/disable_ertm"
    note "Persist: remove disable_ertm from the kernel command line or /etc/modprobe.d"
else
    ok "L2CAP ERTM enabled"
fi

# --- verdict -------------------------------------------------------------
echo
echo "Verdict"
if (( blockers > 0 )); then
    printf '  %sBLOCKED%s — %d hard problem(s) above must be resolved first.\n' "$red" "$rst" "$blockers"
    exit 1
fi

if (( degraded > 0 )); then
    printf '  %sCOMPATIBILITY MODE%s — messages and contacts (MAP/PBAP) should work;\n' "$ylw" "$rst"
    printf '  notification mirroring (ANCS) will not, until the %d capability gap(s) above are fixed.\n' "$degraded"
else
    printf '  %sFULL MODE%s — this machine can carry MAP, PBAP, and ANCS.\n' "$grn" "$rst"
fi

if (( conflicts > 0 )); then
    printf '  %s%d conflict(s)%s must be cleared before Tether can connect right now.\n' "$ylw" "$conflicts" "$rst"
fi
exit 0
