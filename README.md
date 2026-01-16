````markdown
# TCP Echo Server (C++ / Winsock)

A multi-client TCP echo server for Windows built in **C++** using the **Winsock** API.  
It listens on `0.0.0.0:54000`, accepts concurrent client connections, reads incoming bytes, and echoes the exact bytes back to the client.

This repo is a clean baseline for learning socket programming and for extending into more realistic services and protocols.

---

## Features

- Listens on **TCP port 54000**
- Binds to **all interfaces** (`0.0.0.0`)
- Accepts **multiple clients concurrently** (thread-per-client)
- Handles **partial sends** correctly via `sendAll()`
- Logs connect/disconnect events
- Works with **VS Code + MSYS2/MinGW g++** (and a proper `launch.json` for debugging)

---

## Why this project exists

An echo server is the smallest “real” program that forces you to understand:

- TCP connection lifecycle (handshake, teardown)
- The difference between a socket **server** and “packet visualizing”
- TCP being a **byte stream** (no message boundaries)
- How to handle multiple clients
- Why `send()` can return partial writes

---

## How it works (implementation overview)

### 1) Initialize Winsock
Windows requires explicit networking init:

- `WSAStartup(MAKEWORD(2,2), &wsData)` to start
- `WSACleanup()` to shutdown

### 2) Create and configure a listening socket
- `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` creates a TCP socket
- `setsockopt(... SO_REUSEADDR ...)` allows fast restarts
- `bind()` attaches it to `0.0.0.0:54000`
- `listen()` begins accepting incoming connections

### 3) Accept clients in a loop
The main thread calls:

- `accept(listenSocket, ...)`

Each accepted client gets its own `SOCKET` handle.

### 4) Thread-per-client concurrency
Each client connection is handled on a separate thread:

- Main thread: accepts connections
- Worker thread: per-client recv/send loop

This model is simple and ideal for learning; later you can replace it with async IO (IOCP) for scale.

### 5) Receive and echo data
For each client:

- `recv()` reads bytes into a buffer
- the server echoes those bytes back to the client

### 6) `sendAll()` prevents partial-write bugs
A critical detail: `send()` may send fewer bytes than requested. That is normal.

So `sendAll()` loops until **all** bytes are transmitted or an error occurs.

### 7) Clean shutdown
On Ctrl+C, the server:
- flips a shared `running` flag
- closes the listening socket to unblock `accept()`
- client threads exit naturally

---

## Theory behind it (what’s happening on the network)

### TCP is connection-oriented
When a client connects, TCP performs the 3-way handshake:
1. `SYN` (client → server)
2. `SYN-ACK` (server → client)
3. `ACK` (client → server)

Only then does application data flow.

### TCP is a byte stream (no message boundaries)
TCP does not preserve your “messages.” If the client sends `hello server\n`, the server might receive it as:
- one `recv()` call of N bytes,
- multiple `recv()` calls split up,
- or combined with subsequent sends.

Real protocols add **framing** (length-prefix, delimiter, etc.) when message boundaries matter.

### Understanding `recv()` results
- `> 0` : received bytes
- `== 0` : client closed cleanly
- `== SOCKET_ERROR` : socket error (or reset)

### Why you may see `WSAGetLastError=10054`
`10054` = **WSAECONNRESET** (“connection reset by peer”).  
This often happens when the client closes abruptly (common with quick scripts or force-closed terminals). It’s not a server failure; it’s a network reality you should handle/log appropriately.

---

## What I learned (cybersecurity perspective)

Building a network service teaches practical security instincts:

### 1) Attack surface awareness
- Listening on `0.0.0.0` exposes the service to the network, not just localhost.
- Any open port is a potential entry point. Even an echo server can be stress-tested, fuzzed, or used to probe behavior.

### 2) Protocol mindset
- TCP provides reliable delivery but not message framing.
- Security tools often depend on understanding handshake states, resets, and timeouts.

### 3) Defensive programming habits
- Partial writes (`send()`) are real; “toy code” breaks under real conditions.
- Clean resource handling (close sockets, cleanup on exit) prevents unstable behavior that attackers can exploit.

### 4) Observability
- Logging connections and failure modes is the beginning of intrusion visibility.
- Understanding what disconnect reasons mean (clean close vs reset) is useful when reading real-world network telemetry.

---

## Build & Run (VS Code + MSYS2/MinGW)

### Requirements
- Windows
- MSYS2 (UCRT64) with:
  - `g++`
  - `gdb`
- VS Code with the C/C++ extension

### Key linking requirement
When compiling with MinGW, you must link Winsock:

- `-lws2_32`

Example build command:
```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp -o main.exe -lws2_32
````

> If you use MSVC (`cl.exe`), linking differs, but this repo is configured for MinGW in VS Code.

### Run

Start the server (`F5` if you configured `launch.json`, or run `main.exe` directly).
You should see:

```
[*] Server listening on 0.0.0.0:54000
```

---

## Testing the server

### Quick one-liner (PowerShell)

Run this in a separate PowerShell window **while the server is listening**:

```powershell
$c=New-Object Net.Sockets.TcpClient("127.0.0.1",54000);$s=$c.GetStream();$w=New-Object IO.StreamWriter($s);$r=New-Object IO.StreamReader($s);$w.AutoFlush=$true;$w.WriteLine('hello server');$r.ReadLine();$c.Close()
```

Expected output:

```
hello server
```

### Multiple clients

Open multiple terminals and run the one-liner repeatedly or connect using telnet/netcat.
All clients should work concurrently.

---

