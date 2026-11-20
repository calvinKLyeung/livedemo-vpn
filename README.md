# LiveDemo VPN

![tests](https://github.com/calvinKLyeung/livedemo-vpn/actions/workflows/test.yml/badge.svg)

A minimal VPN in ~400 lines of C, built for two reasons: to learn how VPNs like WireGuard work from the ground up, and to put an app running on my laptop online in seconds, for demos and temporary sharing, without deploying it anywhere.
Client: macOS (`utun`). Server: Linux (`tun`) on EC2.

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

## Setup

- **Server:** launch an EC2 instance with `deploy/cloud-init.sh` as user data, then run `./deploy/push-secrets.sh SERVER_IP`. Fresh instance to working VPN in about 10 minutes. It runs under systemd and survives reboots and crashes.
- **Updates:** every push to `main` that passes the tests deploys automatically, using an SSH key that can only run the deploy script.
- **Mac:** `make install`, put `client.conf` in `/usr/local/etc/livedemo-vpn/`, then run `livedemo-vpn-up` when you need it (nothing starts at boot). It keeps itself alive and reconnects on its own after sleep, a Wi-Fi change or a server restart.
- **Tests:** `make -C src test` (replay window) and `sudo ./tests/tunnel_test.sh` (10 end-to-end checks in network namespaces, including a server restart), both run in CI.

Step-by-step commands: [docs/setup.md](docs/setup.md).

## Live Demo mode

Put a local app online at `http://SERVER_IP:8080`.

```
visitor → SERVER_IP:8080 → tunnel → Mac :3000
```

One-time: open TCP 8080 (Anywhere-IPv4) in the EC2 security group.

```sh
livedemo-vpn-up                                      # start the tunnel
python3 -m http.server 3000                          # app must listen on 0.0.0.0
./scripts/expose.sh   -s SERVER_IP -i KEY.pem 3000   # go live
./scripts/unexpose.sh -s SERVER_IP -i KEY.pem 3000   # stop
```

Live in about 30 seconds : )

**Limitations:** plain HTTP from visitors to the server; your Mac must stay awake and connected.

## Full VPN mode

Send all of the Mac's traffic through the server, not just the tunnel network:

```sh
livedemo-vpn-up                                   # terminal 1: the tunnel
sudo ./scripts/mac-up.sh utunN SERVER_IP          # terminal 2: routes, DNS 1.1.1.1, IPv6 off
curl ifconfig.me                                  # → SERVER_IP
sudo ./scripts/mac-down.sh utunN SERVER_IP        # undo
```

`mac-up.sh` keeps a host route to the server on Wi-Fi, sends everything else into the tunnel with two `/1` routes, points DNS at 1.1.1.1 (reached through the tunnel), and turns IPv6 off, since the tunnel only carries IPv4.

**Leak test** ([Mullvad connection check](https://mullvad.net/en/check)), on UBC Wi-Fi:

| | VPN off | Full VPN mode |
|---|---|---|
| Public IPv4 | UBC, Vancouver | the EC2 server, Ohio |
| DNS resolvers seen from | Vancouver | Ohio (through the tunnel) |
| WebRTC | real campus IP | the server's IP |
| My IPv6 | in use | blocked |

![Before](docs/leak-before.png) ![After](docs/leak-after.png)

## Design

- **UDP:** avoids TCP-over-TCP meltdown, and one datagram = one packet.
- **Portable:** `#ifdef __APPLE__` handles utun's 4-byte header.
- **Handshake:** HELLO and RESPONSE swap fresh X25519 keys, signed with each side's long-term Ed25519 key. The server checks an allow-list; the client checks a pinned server key.
- **Packet:** `[type 1][counter 8][nonce 24][ciphertext + tag 16]`, with MTU 1400.
- **Crypto:** XChaCha20-Poly1305 (libsodium) with fresh send and receive keys per session. The header is authenticated but not encrypted.
- **Keepalive and reconnect:** both sides send an empty encrypted packet after 15 s of silence, so routers don't forget the UDP flow; if the client hears nothing for 45 s it opens a new socket and redoes the handshake.
- **Replay protection:** a 64-packet sliding window (one `uint64_t` bitmap, as in IPsec) accepts late or reordered packets but drops any counter already seen. Unit-tested with `make -C src test`.
- **Config:** settings live in a small `key = value` file (`deploy/server.conf`, `deploy/client.conf.example`); the server runs under systemd, and `livedemo-vpn-net-up` re-applies forwarding and NAT before each start.

## Results

Mac on Wi-Fi to EC2 (the original server, before the rebuild), all measured in one session. Throughput: median of 5 × 10 s `iperf3` runs (the three setups alternated each round). Latency: min RTT of `ping -c 50`.

| | Throughput | Min RTT |
|---|---|---|
| Direct | 144 Mbit/s | 112.9 ms |
| livedemo-vpn | 48 Mbit/s | 113.6 ms |
| WireGuard | 85 Mbit/s | 112.7 ms |

![Throughput](bench/throughput.png)

**Recovery** (watched from the server: seconds until the exposed app answered again):

| Event | Recovery |
|---|---|
| Lid closed for 1 min, then opened | X s |
| Wi-Fi → phone hotspot | X s |
| Server restart | X s |

livedemo-vpn adds under 1 ms of latency but loses throughput to per-packet overhead: several system calls and copies per packet on one thread. WireGuard avoids much of this by batching packets (wireguard-go) or running in the kernel.

## Limitations

One client at a time. After a server restart or deploy, the client takes up to about 45 s to notice and reconnect. Packets more than 64 behind are dropped, no re-keying during long sessions. IPv4 only: in full VPN mode IPv6 is blocked rather than tunnelled. No kill switch: if the client crashes or is stopped in full VPN mode without `mac-down.sh`, traffic falls back to plain Wi-Fi without warning (blocking that would need macOS's packet filter, pf; future work). A replayed old HELLO can't reveal any keys, but it replaces the real client's session and cuts it off until it reconnects (WireGuard adds a timestamp to prevent this). The handshake is a simplified design, not WireGuard's Noise.

## What I learned

A VPN turned out to be simpler than I expected at its core: a tun device is just a network interface whose "wire" is a file descriptor, and the VPN is a loop that encrypts packets between it and a UDP socket. The hard parts were everything around that loop: routing (a missing host route makes the tunnel send its own packets into itself), NAT on the server, and getting nonces and replay counters right. Benchmarking showed the cost is per packet (system calls and copies), not encryption.

One surprise: after restarting only the client, 29 of 30 pings failed. The server still remembered the highest counter from the previous session, so it dropped every new packet as a replay until the counter passed the old value. The replay protection was working exactly as designed, which showed me why real VPNs use a handshake to start each session with fresh keys and counters. Adding one fixed it: a restarted client now just reconnects.

I also learned that a VPN hides traffic from outer firewalls but not from the server's own: AWS only saw encrypted UDP on port 5555, while ufw saw the unwrapped TCP from the tunnel and dropped it until I allowed `tun0`.