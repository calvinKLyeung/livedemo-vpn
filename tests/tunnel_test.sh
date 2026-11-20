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
  cat > "$TMP/server.conf" <<EOF
port = 5555
tun_addr = 10.8.0.1/24
sign_key = $TMP/srv.sk
authorized_clients = $TMP/allowed
EOF
  ip netns exec ld-srv $BIN -s "$TMP/server.conf" >"$TMP/server.log" 2>&1 & SRV=$!
  wait_for_tun ld-srv
}

start_client() {               # start_client KEY PINNED [flags...]
  local key=$1 pinned=$2; shift 2
  cat > "$TMP/client.conf" <<EOF
server = 192.168.100.1
port = 5555
tun_addr = 10.8.0.2/24
tun_peer = 10.8.0.1
sign_key = $key
server_key = $pinned
EOF
  ip netns exec ld-cli $BIN -c "$TMP/client.conf" "$@" >"$TMP/client.log" 2>&1 & CLI=$!
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


# SET UP: throwaway identities for this run
./src/keygen "$TMP/srv"      >/dev/null
./src/keygen "$TMP/cli"      > "$TMP/allowed"     # the client's public key is the allow-list
./src/keygen "$TMP/stranger" >/dev/null           # a client that isn't allowed
./src/keygen "$TMP/fakesrv"  >/dev/null           # an impostor server

# SERVER starts once and stays up for every test
start_server

# 1-2. basic tunnel and a big transfer (tests MTU and buffers)
start_client "$TMP/cli.sk" "$TMP/srv.pk"
check "ping through the tunnel" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
head -c 5000000 /dev/urandom > "$TMP/big.bin"
ip netns exec ld-srv python3 -m http.server 8000 --bind 10.8.0.1 --directory "$TMP" >/dev/null 2>&1 & WEB=$!
sleep 1
check "5 MB download arrives intact" bash -c "ip netns exec ld-cli curl -s --max-time 30 http://10.8.0.1:8000/big.bin | cmp - '$TMP/big.bin'"
kill $WEB
stop_client

# 3. tampered packets are rejected (--tamper flips a bit in every 10th packet)
start_client "$TMP/cli.sk" "$TMP/srv.pk" --tamper
ip netns exec ld-cli ping -c 20 -i 0.2 -W 1 10.8.0.1 >/dev/null 2>&1
check "server rejects tampered packets" grep -q "dropped bad packet" "$TMP/server.log"
stop_client

# 4-5. replays are rejected, real traffic still works (--replay sends every packet twice)
start_client "$TMP/cli.sk" "$TMP/srv.pk" --replay
check "ping works while replays are sent" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
check "server rejects replays" grep -q "dropped replay" "$TMP/server.log"
stop_client

# 6. a restarted client just reconnects
start_client "$TMP/cli.sk" "$TMP/srv.pk"
check "client restart reconnects" ip netns exec ld-cli ping -c 3 -W 2 10.8.0.1
stop_client

# 7. the client refuses an impostor server
start_client "$TMP/cli.sk" "$TMP/fakesrv.pk"; sleep 3
check "client refuses an impostor server" grep -q "bad server signature" "$TMP/client.log"
stop_client

# 8. the server ignores clients not on the allow-list
start_client "$TMP/stranger.sk" "$TMP/srv.pk"; sleep 3
check "server ignores unknown clients" grep -q "unknown client" "$TMP/server.log"
stop_client

# 9-10. server restart: the client notices the silence and reconnects by itself
#       last, because restarting the server wipes server.log
start_client "$TMP/cli.sk" "$TMP/srv.pk"; wait_for_session
kill $SRV; wait $SRV 2>/dev/null
start_server
heal() {                       # up to 60 s: 45 s silence check + handshake
  for i in $(seq 1 60); do
    ip netns exec ld-cli ping -c 1 -W 1 10.8.0.1 >/dev/null 2>&1 && return 0
  done
  return 1
}
check "client reconnects after a server restart" heal
check "client logged the reconnect" grep -q "tunnel silent, reconnecting" "$TMP/client.log"
stop_client

echo; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]