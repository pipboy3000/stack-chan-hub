#!/bin/sh
# Prints the hub's log lines, broadcast over UDP port 5140 while on Wi-Fi.
# (macOS nc -u sticks to the first sender and misses logs after a reboot.)
exec python3 -u -c '
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", 5140))
while True:
    data, _ = s.recvfrom(2048)
    print(data.decode(errors="replace"), end="", flush=True)
'
