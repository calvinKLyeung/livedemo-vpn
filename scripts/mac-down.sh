#!/bin/sh
# scripts/mac-down.sh   usage: sudo ./scripts/mac-down.sh utun5 SERVER_IP
IF="$1"; SERVER="$2"
route -n delete -net 0.0.0.0/1   -interface "$IF" 2>/dev/null
route -n delete -net 128.0.0.0/1 -interface "$IF" 2>/dev/null
route -n delete -host "$SERVER" 2>/dev/null
networksetup -setdnsservers Wi-Fi empty
networksetup -setv6automatic Wi-Fi
echo "Full VPN mode off."