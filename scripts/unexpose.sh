#!/bin/sh
# scripts/unexpose.sh: take a local app offline again
# usage: ./scripts/unexpose.sh [-s SERVER_IP] [-i KEY.pem] LOCAL_PORT [PUBLIC_PORT]
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

ssh ${KEY:+-i "$KEY"} ubuntu@"$SERVER" sudo sh -s <<EOF
iptables -t nat -D PREROUTING -i ens5 -p tcp --dport $PUBLIC -j DNAT --to-destination 10.8.0.2:$LOCAL 2>/dev/null || true
ufw route delete allow in on ens5 out on tun0 to 10.8.0.2 port $LOCAL proto tcp >/dev/null || true
EOF

echo "Stopped. http://$SERVER:$PUBLIC no longer reaches your Mac."