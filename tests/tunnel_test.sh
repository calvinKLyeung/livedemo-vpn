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