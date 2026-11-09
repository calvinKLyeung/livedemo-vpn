# mini-vpn





## Self-tests

Restart both ends before each test.

**Tamper** – flips one bit in every 10th packet:
```
sudo ./mini-vpn -c SERVER_IP --tamper
ping -c 30 10.8.0.1   # expect 10% loss; server shows bad_packets=3
```

**Replay** – sends every packet twice:
```
sudo ./mini-vpn -c SERVER_IP --replay
ping -c 30 10.8.0.1   # expect 0% loss, no (DUP!); server shows replays=30
```