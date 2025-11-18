# CMPE 275 – Mini 2  
### Distributed Message Routing + CSV Job Execution  
*(Version prior to FireStats)*

This mini implements a small overlay of gRPC-based C++ nodes that can:

- Load an overlay from `config.json`
- Connect to peers via a HELLO handshake
- Forward messages across the overlay using `src` / `dest`
- Process jobs using worker threads
- Execute a CSV job (`getAvgPop`) on the correct node

---

## 1. Build

```bash
mkdir -p build
cd build
cmake ..
make -j
```

Produces:

```
loop-server
loop-client
```

---

## 2. Configuration

Nodes read `config.json` to learn:

- their node ID (A–F)
- listening address
- peers in the overlay

Example:

```json
"A": { "addr": "127.0.0.1:50051", "peers": ["B", "E"] }
```

Each node creates gRPC stubs and sends `__HELLO__` to peers.

---

## 3. Start Servers

Open separate terminals and run from `build/`:

```bash
./loop-server A
./loop-server B
./loop-server C
./loop-server D
./loop-server E
./loop-server F
```

Expected HELLO logs:

```
[Node] Stub for Peer B created at 127.0.0.1:50052
[Node A] Sending HELLO to peer B
[Node B] Connected to peer A
[Node B] HELLO ack from peer A
```

---

## 4. Client Usage

```bash
./loop-client <serverAddr> <srcNode> <destNode> <payload>
```

- `serverAddr`: address of any running node (ex: `127.0.0.1:50051`)
- `srcNode`: usually `CLIENT`
- `destNode`: one of A–F
- `payload`: `"hi"` or `"getAvgPop"`

---

## 5. Examples and Logs

### 5.1 Routing a Simple Message

```bash
./loop-client 127.0.0.1:50051 CLIENT F hi
```

A:

```
[Node A][Worker 3] Msg received from [Node CLIENT]: hi
[Node A] Forwarding Msg to Peer Node B
```

B:

```
[Node B][Worker 1] Msg received from [Node A]: hi
[Node B] Forwarding Msg to Peer Node F
```

F:

```
[Node F][Worker 0] Msg received from [Node B]: hi
Msg delivered to F (worker 0)
```

---

### 5.2 Running the CSV Population Job

```bash
./loop-client 127.0.0.1:50051 CLIENT A getAvgPop
```

Server output:

```
[Worker 2] Attempting to read...
[Worker 2] Read in 266 rows
[Worker 2] Performing calculations
[Worker 2] Completed. Returning results
```

CSV Location:

```
../dataset/world/populations.csv
```

Client output (example):

```
United States:178395.22
France:120392.11
Japan:199444.89
...
```

---

## 6. Runtime Measurements

Each processed job logs its execution time:

```
Execution time:: 73792ns
Execution time:: 117292ns
```

This reflects:

- Forwarding (`hi`) → ~70–120 microseconds  
- CSV read job (`getAvgPop`) → a few ms  
- Concurrency via multiple worker threads  

---

## 7. Shutdown

Press Ctrl+C in each server terminal:

```
Shutting down node A...
```

Client exits after each request.

---

## 8. Repository Layout

```
src/            – queue, workers, routing, job logic
config/         – config.json
dataset/        – world/populations.csv
build/          – compiled binaries
```
