# LiveDemo VPN

![tests](https://github.com/calvinKLyeung/livedemo-vpn/actions/workflows/test.yml/badge.svg)

A minimal VPN in ~400 lines of C, built to understand how tools like WireGuard work.
Client: macOS (`utun`). Server: Linux (`tun`). Learning project, not for real use.

## How it works

**Outgoing (app → internet)**

```
app (e.g. curl example.com)
 → Mac routing table sends the packet into utun (10.8.0.2)
 → livedemo-vpn reads it, encrypts it, adds a header
 → UDP :5555 over Wi-Fi to the server
 → livedemo-vpn verifies, decrypts, writes it into tun0 (10.8.0.1)
 → server forwards it, NAT swaps 10.8.0.2 for the server's public IP
 → ens5 → internet
```

**Incoming (internet → app)**

```
example.com replies to the server's public IP
 → NAT recognizes the connection and swaps the destination back to 10.8.0.2
 → server routes it into tun0
 → livedemo-vpn reads it, encrypts it, sends it over UDP to the Mac
 → livedemo-vpn on the Mac verifies, decrypts, writes it into utun
 → Mac kernel delivers it to the app
```

A single `poll()` loop on each side watches both the tunnel fd and the UDP socket, so one thread handles both directions.

## Quick start

**First-time setup** (run everything from the repo root)

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
sudo livedemo-vpn -c /usr/local/etc/livedemo-vpn/client.conf
ping 10.8.0.1                                     # test the tunnel
```

## New server in one command

Fresh EC2 instance to working VPN in about 10 minutes, with no manual setup on the server.

1. Launch Ubuntu 24.04 (t3.micro, same security group) with `deploy/cloud-init.sh` as **User data**. On first boot it installs packages, sets up the firewall and forwarding, builds and installs the VPN, and enables the service.
2. Wait for it, then push the one secret it can't get from the repo:
   ```sh
   ssh ubuntu@SERVER_IP 'cloud-init status --wait'
   ./deploy/push-secrets.sh SERVER_IP      # copies server.sk from ~/.ldvpn and starts the VPN
   ```
3. Move the Elastic IP to the new instance, so the Mac's `client.conf` doesn't change.

The server's identity key is backed up on the Mac (`~/.ldvpn/server.sk`, never in the repo) and reused on every rebuild, so the Mac's pinned `server.pk` keeps matching.

## Live Demo mode

Put a local app online at `http://SERVER_IP:8080`.

```
visitor → SERVER_IP:8080 → tunnel → Mac :3000
```

One-time: open TCP 8080 (Anywhere-IPv4) in the EC2 security group.

```sh
ping 10.8.0.1                                        # keep the tunnel awake
python3 -m http.server 3000                          # app must listen on 0.0.0.0
./scripts/expose.sh   -s SERVER_IP -i KEY.pem 3000   # go live
./scripts/unexpose.sh -s SERVER_IP -i KEY.pem 3000   # stop
```

Live in about 30 seconds : )

**Limitations:** plain HTTP from visitors to the server; your Mac must stay awake and connected.

## Design

- **UDP:** avoids TCP-over-TCP meltdown, and one datagram = one packet.
- **Portable:** `#ifdef __APPLE__` handles utun's 4-byte header.
- **Handshake:** HELLO and RESPONSE swap fresh X25519 keys, signed with each side's long-term Ed25519 key. The server checks an allow-list; the client checks a pinned server key.
- **Packet:** `[type 1][counter 8][nonce 24][ciphertext + tag 16]`, with MTU 1400.
- **Crypto:** XChaCha20-Poly1305 (libsodium) with fresh send and receive keys per session. The header is authenticated but not encrypted.
- **Replay protection:** a 64-packet sliding window (one `uint64_t` bitmap, as in IPsec) accepts late or reordered packets but drops any counter already seen. Unit-tested with `make -C src test`.
- **Config:** settings live in a small `key = value` file (`deploy/server.conf`, `deploy/client.conf.example`); the server runs under systemd, and `livedemo-vpn-net-up` re-applies forwarding and NAT before each start.

## Self-tests

`sudo ./tests/tunnel_test.sh` runs 9 checks in network namespaces (also on every push via GitHub Actions). By hand:

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

## Results

Mac on Wi-Fi to EC2 (the original server, before the rebuild), all measured in one session. Throughput: median of 5 × 10 s `iperf3` runs (the three setups alternated each round). Latency: min RTT of `ping -c 50`.

| | Throughput | Min RTT |
|---|---|---|
| Direct | 144 Mbit/s | 112.9 ms |
| livedemo-vpn | 48 Mbit/s | 113.6 ms |
| WireGuard | 85 Mbit/s | 112.7 ms |

![Throughput](bench/throughput.png)

livedemo-vpn adds under 1 ms of latency but loses throughput to per-packet overhead: several system calls and copies per packet on one thread. WireGuard avoids much of this by batching packets (wireguard-go) or running in the kernel.

## Limitations

One client at a time. If the server restarts, the Mac client must be restarted too (it doesn't notice the lost session yet). Packets more than 64 behind are dropped, no re-keying during long sessions, IPv4 only. A replayed old HELLO can't reveal any keys, but it replaces the real client's session and cuts it off until it reconnects (WireGuard adds a timestamp to prevent this). The handshake is a simplified design, not WireGuard's Noise.

## What I learned

A VPN turned out to be simpler than I expected at its core: a tun device is just a network interface whose "wire" is a file descriptor, and the VPN is a loop that encrypts packets between it and a UDP socket. The hard parts were everything around that loop: routing (a missing host route makes the tunnel send its own packets into itself), NAT on the server, and getting nonces and replay counters right. Benchmarking showed the cost is per packet (system calls and copies), not encryption.

One surprise: after restarting only the client, 29 of 30 pings failed. The server still remembered the highest counter from the previous session, so it dropped every new packet as a replay until the counter passed the old value. The replay protection was working exactly as designed, which showed me why real VPNs use a handshake to start each session with fresh keys and counters. Adding one fixed it: a restarted client now just reconnects.

I also learned that a VPN hides traffic from outer firewalls but not from the server's own: AWS only saw encrypted UDP on port 5555, while ufw saw the unwrapped TCP from the tunnel and dropped it until I allowed `tun0`.