#!/usr/bin/env python3
"""Simple TCP CLI client for ESP32P4 BUS (port 2323)."""
import socket
import sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.4.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 2323

def main():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(0.3)
    print(s.recv(4096).decode("utf-8", "ignore"), end="")
    while True:
        try:
            line = input()
        except EOFError:
            break
        if not line:
            continue
        s.sendall((line + "\n").encode())
        buf = b""
        while True:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                buf += chunk
                if b"> " in buf:
                    break
            except socket.timeout:
                break
        sys.stdout.write(buf.decode("utf-8", "ignore"))
        sys.stdout.flush()
    s.close()

if __name__ == "__main__":
    main()
