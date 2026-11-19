#!/bin/sh
# usage: ./deploy/push-secrets.sh SERVER_IP
set -e
HOST=$1
scp ~/.ldvpn/server.sk ubuntu@"$HOST":/tmp/server.sk
ssh ubuntu@"$HOST" 'sudo install -m 600 /tmp/server.sk /etc/livedemo-vpn/server.sk && rm /tmp/server.sk && sudo systemctl start livedemo-vpn && systemctl is-active livedemo-vpn'