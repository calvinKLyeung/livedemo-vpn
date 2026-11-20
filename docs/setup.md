# Setup and operations

Everything needed to run, rebuild and deploy LiveDemo VPN. For what it is and how it works, see the [README](../README.md).

## First-time setup

Run everything from the repo root.

On the server:
```sh
make -C src && sudo make -C src install           # → /usr/local/bin/livedemo-vpn
src/keygen src/server && chmod 600 src/server.sk
```

On the Mac:
```sh
make -C src && sudo make -C src install
src/keygen src/mac && chmod 600 src/mac.sk     # prints the Mac's public key
scp -i KEY.pem ubuntu@SERVER_IP:~/livedemo-vpn/src/server.pk src/   # copy the server's public key
```

Back on the server, allow the Mac:
```sh
echo "MAC_PUBLIC_KEY_HEX" > src/authorized_clients
```

**Server as a service** (starts on boot, restarts on crash)
```sh
sudo mkdir -p /etc/livedemo-vpn
sudo install -m 600 src/server.sk /etc/livedemo-vpn/
sudo install -m 644 src/authorized_clients deploy/server.conf /etc/livedemo-vpn/
sudo install -m 755 deploy/livedemo-vpn-net-up /usr/local/sbin/
sudo install -m 644 deploy/livedemo-vpn.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now livedemo-vpn
journalctl -u livedemo-vpn -f                     # logs
```

**Mac client**
```sh
sudo mkdir -p /usr/local/etc/livedemo-vpn
sudo install -m 600 src/mac.sk /usr/local/etc/livedemo-vpn/
sudo install -m 644 src/server.pk /usr/local/etc/livedemo-vpn/
sudo cp deploy/client.conf.example /usr/local/etc/livedemo-vpn/client.conf   # then set server = SERVER_IP
sudo install -d /usr/local/sbin
sudo install -m 755 scripts/livedemo-vpn-up /usr/local/bin/livedemo-vpn-up
sudo install -m 755 scripts/mac-up.sh       /usr/local/sbin/livedemo-vpn-mac-up
sudo install -m 755 scripts/mac-down.sh     /usr/local/sbin/livedemo-vpn-mac-down
livedemo-vpn-up                                   # tunnel only (Live Demo mode); Ctrl+C to stop
livedemo-vpn-up --full                            # all traffic through the server; Ctrl+C undoes it
ping 10.8.0.1                                     # test it (second terminal)
```


## Rebuild a server in one command

Fresh EC2 instance to working VPN in about 10 minutes, with no manual setup on the server.

1. Launch Ubuntu 24.04 (t3.micro, same security group) with `deploy/cloud-init.sh` as **User data**. On first boot it installs packages, sets up the firewall and forwarding, builds and installs the VPN, and enables the service.
2. Wait for it, then push the one secret it can't get from the repo:
   ```sh
   ssh ubuntu@SERVER_IP 'cloud-init status --wait'
   ./deploy/push-secrets.sh SERVER_IP      # copies server.sk from ~/.ldvpn and starts the VPN
   ```
3. Move the Elastic IP to the new instance, so the Mac's `client.conf` doesn't change.
4. For auto-deploy: add the deploy key line to the new server's `~/.ssh/authorized_keys`, and update the `KNOWN_HOSTS` secret (a new instance has a new host key).

The server's identity key is backed up on the Mac (`~/.ldvpn/server.sk`, never in the repo) and reused on every rebuild, so the Mac's pinned `server.pk` keeps matching.


## Auto-deploy on push

Every push to `main` that passes both test jobs updates the live server:

```
git push → tests (Linux tunnel + macOS build) → deploy job → "deployed 022c8be"
```

The deploy job SSHes in with a dedicated key that can do only one thing: in the server's `authorized_keys` it is locked to `restrict,command="sudo /usr/local/sbin/livedemo-vpn-deploy"`, so it can't open a shell even if it leaks. The script (`deploy/livedemo-vpn-deploy`) resets `/opt/livedemo-vpn` to `origin/main`, does a clean build, installs, and restarts the service. A failing test means no deploy.

The build embeds the commit hash, so the server reports what it's running:
```sh
ssh ubuntu@SERVER_IP 'journalctl -u livedemo-vpn | grep starting | tail -1'
# livedemo-vpn 022c8be starting
```

GitHub secrets: `DEPLOY_KEY` (the deploy key's private half), `SERVER_HOST` (the Elastic IP), `KNOWN_HOSTS` (`ssh-keyscan -t ed25519 SERVER_IP`).


## Self-tests

`sudo ./tests/tunnel_test.sh` runs 10 checks in network namespaces (also on every push via GitHub Actions). By hand:

**Tamper:** flips one bit in every 10th packet.
```sh
sudo livedemo-vpn -c /usr/local/etc/livedemo-vpn/client.conf --tamper
ping -c 30 10.8.0.1   # expect 10% loss; server shows bad_packets=3
```

**Replay:** sends every packet twice.
```sh
sudo livedemo-vpn -c /usr/local/etc/livedemo-vpn/client.conf --replay
ping -c 30 10.8.0.1   # expect 0% loss, no (DUP!); server shows replays=30
```