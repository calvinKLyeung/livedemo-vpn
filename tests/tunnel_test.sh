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



start_server() {               # once, at the start
  ip netns exec ld-srv $BIN -s -k "$TMP/srv.sk" -a "$TMP/allowed" >"$TMP/server.log" 2>&1 & SRV=$!
  wait_for_tun ld-srv
}

start_client() {               # start_client KEY PINNED [flags...]
  local key=$1 pinned=$2; shift 2
  ip netns exec ld-cli $BIN -c 192.168.100.1 -k "$key" -p "$pinned" "$@" >"$TMP/client.log" 2>&1 & CLI=$!
  wait_for_tun ld-cli
}

wait_for_session() {           # until the client logs "session up" (max 5 s)
  for i in $(seq 1 50); do
    grep -q "session up" "$TMP/client.log" && return 0
    sleep 0.1
  done
  return 1
}

stop_client() { kill $CLI 2>/dev/null; wait $CLI 2>/dev/null; }


# SET UP 
./src/keygen "$TMP/srv"      >/dev/null
./src/keygen "$TMP/cli"      > "$TMP/allowed"     # the client's public key is the allow-list
./src/keygen "$TMP/stranger" >/dev/null
./src/keygen "$TMP/fakesrv"  >/dev/null
# server: $BIN -s -k "$TMP/srv.sk" -a "$TMP/allowed"
# client: $BIN -c 192.168.100.1 -k CLIENT.sk -p PINNED.pk

# SERVER starts 
start_server 
# 1-2. basic tunnel and a big transfer (tests MTU and buffers)
# Check if tunnel works at all
# ping 10.8.0.1 from the client and check if exits with 0
start_client "$TMP/cli.sk" "$TMP/srv.pk"
check "ping through the tunnel" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
head -c 5000000 /dev/urandom > "$TMP/big.bin"  # make 5mb file
ip netns exec ld-srv python3 -m http.server 8000 --bind 10.8.0.1 --directory "$TMP" >/dev/null 2>&1 & WEB=$!
sleep 1
# Download a 5 MB random file over the tunnel and compare it byte for byte
check "5 MB download arrives intact" bash -c "ip netns exec ld-cli curl -s --max-time 30 http://10.8.0.1:8000/big.bin | cmp - '$TMP/big.bin'"
kill $WEB; 
stop_client

# 3. tampered packets are rejected
# Client runs --tamper (flips a bit in every 10th packet), sends 20 pings
start_client "$TMP/cli.sk" "$TMP/srv.pk" --tamper
ip netns exec ld-cli ping -c 20 -i 0.2 -W 1 10.8.0.1 >/dev/null 2>&1
check "server rejects tampered packets" grep -q "dropped bad packet" "$TMP/server.log"
stop_client

# 4-5. replays are rejected, real traffic still works
# Client runs --replay (sends every packet twice), pings
start_client "$TMP/cli.sk" "$TMP/srv.pk" --replay
check "ping works while replays are sent" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
check "server rejects replays" grep -q "dropped replay" "$TMP/server.log"
stop_client

# 6. unknown client and imposter server 
start_client "$TMP/cli.sk" "$TMP/srv.pk"
check "ping through the tunnel" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
stop_client
start_client "$TMP/cli.sk" "$TMP/srv.pk"
check "client restart reconnects" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
stop_client

start_client "$TMP/cli.sk" "$TMP/fakesrv.pk"; sleep 3
check "client refuses an impostor server" grep -q "bad server signature" "$TMP/client.log"
stop_client

start_client "$TMP/stranger.sk" "$TMP/srv.pk"; sleep 3
check "server ignores unknown clients" grep -q "unknown client" "$TMP/server.log"
stop_client

echo; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]