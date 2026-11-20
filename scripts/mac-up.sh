#!/bin/sh
# scripts/mac-up.sh   usage: sudo ./scripts/mac-up.sh utun5 SERVER_IP
# run while the client is up; (livedemo-vpn-up --full) runs it for you
set -e
IF="$1"; SERVER="$2"
[ -n "$IF" ] && [ -n "$SERVER" ] || { echo "usage: sudo $0 utunN SERVER_IP"; exit 1; }
GW=$(route -n get default | awk '/gateway/ {print $2}')
route -n add -host "$SERVER" "$GW"                 # tunnel packets still use Wi-Fi
route -n add -net 0.0.0.0/1   -interface "$IF"     # everything else: the tunnel
route -n add -net 128.0.0.0/1 -interface "$IF"
networksetup -setdnsservers Wi-Fi 1.1.1.1
networksetup -setv6off Wi-Fi
echo "Full VPN mode on. Check with: curl ifconfig.me"