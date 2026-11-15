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

```sh
make -C src && cd src
./keygen server && chmod 600 server.sk   # server
./keygen mac && chmod 600 mac.sk         # Mac: paste the printed key into the server's authorized_clients
                                         # Mac: copy server.pk from the server

sudo ./livedemo-vpn -s -k server.sk -a authorized_clients   # server (open UDP 5555)
sudo ./livedemo-vpn -c SERVER_IP -k mac.sk -p server.pk     # Mac
ping 10.8.0.1
```

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
- **Replay protection:** the receiver drops any counter it has already seen in the session.

## Self-tests

`sudo ./tests/tunnel_test.sh` runs 9 checks in network namespaces (also on every push via GitHub Actions). By hand:

**Tamper:** flips one bit in every 10th packet.
```sh
sudo ./livedemo-vpn -c SERVER_IP -k mac.sk -p server.pk --tamper
ping -c 30 10.8.0.1   # expect 10% loss; server shows bad_packets=3
```

**Replay:** sends every packet twice.
```sh
sudo ./livedemo-vpn -c SERVER_IP -k mac.sk -p server.pk --replay
ping -c 30 10.8.0.1   # expect 0% loss, no (DUP!); server shows replays=30
```

## Results

Mac on Wi-Fi to EC2, all measured in one session. Throughput: median of 5 × 10 s `iperf3` runs (the three setups alternated each round). Latency: min RTT of `ping -c 50`.

| | Throughput | Min RTT |
|---|---|---|
| Direct | 144 Mbit/s | 112.9 ms |
| livedemo-vpn | 48 Mbit/s | 113.6 ms |
| WireGuard | 85 Mbit/s | 112.7 ms |

![Throughput](bench/throughput.png)

livedemo-vpn adds under 1 ms of latency but loses throughput to per-packet overhead: several system calls and copies per packet on one thread. WireGuard avoids much of this by batching packets (wireguard-go) or running in the kernel.

## Limitations

One client at a time, no replay window, no re-keying during long sessions, IPv4 only. A replayed old HELLO can't reveal any keys, but it replaces the real client's session and cuts it off until it reconnects (WireGuard adds a timestamp to prevent this). The handshake is a simplified design, not WireGuard's Noise.

## What I learned

A VPN turned out to be simpler than I expected at its core: a tun device is just a network interface whose "wire" is a file descriptor, and the VPN is a loop that encrypts packets between it and a UDP socket. The hard parts were everything around that loop: routing (a missing host route makes the tunnel send its own packets into itself), NAT on the server, and getting nonces and replay counters right. Benchmarking showed the cost is per packet (system calls and copies), not encryption.

One surprise: after restarting only the client, 29 of 30 pings failed. The server still remembered the highest counter from the previous session, so it dropped every new packet as a replay until the counter passed the old value. The replay protection was working exactly as designed, which showed me why real VPNs use a handshake to start each session with fresh keys and counters. Adding one fixed it: a restarted client now just reconnects.

I also learned that a VPN hides traffic from outer firewalls but not from the server's own: AWS only saw encrypted UDP on port 5555, while ufw saw the unwrapped TCP from the tunnel and dropped it until I allowed `tun0`.