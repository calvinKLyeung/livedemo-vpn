#!/bin/bash
# deploy/cloud-init.sh: turns a fresh Ubuntu EC2 instance into a LiveDemo VPN server
set -euxo pipefail
REPO=https://github.com/calvinKLyeung/livedemo-vpn.git

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential pkg-config libsodium-dev git iperf3 tcpdump

# firewall
ufw allow OpenSSH
ufw allow 5555/udp
ufw route allow in on tun0 out on ens5
ufw --force enable

# forwarding that survives reboots
echo 'net.ipv4.ip_forward=1' > /etc/sysctl.d/99-livedemo-vpn.conf
sysctl --system

# build and install
git clone "$REPO" /opt/livedemo-vpn
make -C /opt/livedemo-vpn/src
make -C /opt/livedemo-vpn/src install
install -m 755 /opt/livedemo-vpn/deploy/livedemo-vpn-net-up /usr/local/sbin/livedemo-vpn-net-up
# install -m 755 /opt/livedemo-vpn/deploy/livedemo-vpn-deploy /usr/local/sbin/livedemo-vpn-deploy
install -d -m 700 /etc/livedemo-vpn
install -m 644 /opt/livedemo-vpn/deploy/server.conf /etc/livedemo-vpn/server.conf
install -m 644 /opt/livedemo-vpn/deploy/authorized_clients /etc/livedemo-vpn/authorized_clients
install -m 644 /opt/livedemo-vpn/deploy/livedemo-vpn.service /etc/systemd/system/livedemo-vpn.service
systemctl daemon-reload
systemctl enable livedemo-vpn      # starts once the secret key is in place