#!/bin/sh
# scripts/expose.sh: put a local app online through the tunnel
# usage: ./scripts/expose.sh [-s SERVER_IP] [-i KEY.pem] LOCAL_PORT [PUBLIC_PORT]
#   SERVER_IP and KEY can also come from $LDVPN_SERVER and $LDVPN_KEY
set -e
usage() { echo "usage: $0 [-s SERVER_IP] [-i KEY.pem] LOCAL_PORT [PUBLIC_PORT]"; exit 1; }

SERVER=${LDVPN_SERVER:-}
KEY=${LDVPN_KEY:-}
while getopts s:i: opt; do
  case $opt in
    s) SERVER=$OPTARG ;;
    i) KEY=$OPTARG ;;
    *) usage ;;
  esac
done
shift $((OPTIND - 1))

LOCAL=$1
PUBLIC=${2:-8080}
[ -n "$LOCAL" ]  || usage
[ -n "$SERVER" ] || { echo "No server: use -s SERVER_IP or export LDVPN_SERVER=..."; exit 1; }

ping -c 1 -t 2 10.8.0.1 >/dev/null 2>&1 || { echo "Tunnel is down. Start sudo ./src/livedemo-vpn -c $SERVER first."; exit 1; }
curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$LOCAL" || echo "Warning: nothing answered on localhost:$LOCAL"

ssh ${KEY:+-i "$KEY"} ubuntu@"$SERVER" sudo sh -s <<EOF
sysctl -q -w net.ipv4.ip_forward=1
iptables -t nat -C PREROUTING -i ens5 -p tcp --dport $PUBLIC -j DNAT --to-destination 10.8.0.2:$LOCAL 2>/dev/null ||
  iptables -t nat -A PREROUTING -i ens5 -p tcp --dport $PUBLIC -j DNAT --to-destination 10.8.0.2:$LOCAL
iptables -t nat -C POSTROUTING -o tun0 -j MASQUERADE 2>/dev/null ||
  iptables -t nat -A POSTROUTING -o tun0 -j MASQUERADE
ufw route allow in on ens5 out on tun0 to 10.8.0.2 port $LOCAL proto tcp >/dev/null
EOF

echo "Live at http://$SERVER:$PUBLIC  (stop with ./scripts/unexpose.sh -s $SERVER $LOCAL $PUBLIC)"
echo "Keep 'ping 10.8.0.1' running so the tunnel stays awake."