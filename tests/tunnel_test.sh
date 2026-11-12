#!/bin/bash
# tests/tunnel_test.sh   run as root: sudo ./tests/tunnel_test.sh
set -u
cd "$(dirname "$0")/.."
BIN=./src/livedemo-vpn
TMP=$(mktemp -d)
PASS=0; FAIL=0; SRV=""; CLI=""

cleanup() {
  kill $(jobs -p) 2>/dev/null; wait 2>/dev/null
  if [ "$FAIL" -gt 0 ]; then echo "--- logs ---"; tail -n 20 "$TMP"/*.log; fi
  ip netns del ld-srv 2>/dev/null; ip netns del ld-cli 2>/dev/null
  rm -rf "$TMP"
}
trap cleanup EXIT

check() {                      # check "name" command...
  local name=$1; shift
  if "$@" >/dev/null 2>&1; then echo "PASS  $name"; PASS=$((PASS+1))
  else echo "FAIL  $name"; FAIL=$((FAIL+1)); fi
}

wait_for_tun() {               # wait_for_tun NAMESPACE: until livedemo-vpn has given tun0 its address
  for i in $(seq 1 50); do
    ip -n "$1" addr show tun0 2>/dev/null | grep -q 'inet 10\.8\.0\.' && return 0
    sleep 0.1
  done
  return 1
}

# two "machines" joined by a virtual ethernet cable veth
ip netns add ld-srv; ip netns add ld-cli       # two empty name spaces 
ip link add veth-srv type veth peer name veth-cli   # make veth cable 
ip link set veth-srv netns ld-srv; ip link set veth-cli netns ld-cli   # plug in 2 ends to 2 name spaces 
ip -n ld-srv addr add 192.168.100.1/24 dev veth-srv                     # assign IP
ip -n ld-cli addr add 192.168.100.2/24 dev veth-cli                     # assign IP
for ns in ld-srv ld-cli; do ip -n $ns link set lo up; done              # fire up 
ip -n ld-srv link set veth-srv up; ip -n ld-cli link set veth-cli up



start_pair() {                 # start_pair NAME CLIENT_KEY [client flags...], fresh every time 
  local name=$1 key=$2; shift 2
  ip netns exec ld-srv $BIN -s -k "$TMP/good.key" >"$TMP/$name.server.log" 2>&1 & SRV=$!  # start server 
  wait_for_tun ld-srv || return 1
  ip netns exec ld-cli $BIN -c 192.168.100.1 -k "$key" "$@" >"$TMP/$name.client.log" 2>&1 & CLI=$!   # start client 
  wait_for_tun ld-cli || return 1
}

stop_pair() { kill $CLI $SRV 2>/dev/null; wait $CLI $SRV 2>/dev/null; }



./src/keygen "$TMP/good.key" >/dev/null
./src/keygen "$TMP/wrong.key" >/dev/null

# 1-2. basic tunnel and a big transfer (tests MTU and buffers)
# Check if tunnel works at all
# ping 10.8.0.1 from the client and check if exits with 0
start_pair basic "$TMP/good.key"
check "ping through the tunnel" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
head -c 5000000 /dev/urandom > "$TMP/big.bin"  # make 5mb file
ip netns exec ld-srv python3 -m http.server 8000 --bind 10.8.0.1 --directory "$TMP" >/dev/null 2>&1 & WEB=$!
sleep 1
# Download a 5 MB random file over the tunnel and compare it byte for byte
check "5 MB download arrives intact" bash -c "ip netns exec ld-cli curl -s --max-time 30 http://10.8.0.1:8000/big.bin | cmp - '$TMP/big.bin'"
kill $WEB; stop_pair

# 3. tampered packets are rejected
# Client runs --tamper (flips a bit in every 10th packet), sends 20 pings
start_pair tamper "$TMP/good.key" --tamper
ip netns exec ld-cli ping -c 20 -i 0.2 -W 1 10.8.0.1 >/dev/null 2>&1
check "server rejects tampered packets" grep -q "dropped bad packet" "$TMP/tamper.server.log"
stop_pair

# 4-5. replays are rejected, real traffic still works
# Client runs --replay (sends every packet twice), pings
start_pair replay "$TMP/good.key" --replay
check "ping works while replays are sent" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
check "server rejects replays" grep -q "dropped replay" "$TMP/replay.server.log"
stop_pair

# 6. the wrong key gets nowhere
# Client uses wrong.key
start_pair wrongkey "$TMP/wrong.key"
check "wrong key cannot ping" bash -c "! ip netns exec ld-cli ping -c 2 -W 1 10.8.0.1"
stop_pair

echo; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]