# Client-Server Application

This application implements a **client-server system** where TCP clients (subscribers) can subscribe to topics published by UDP clients (publishers).

The application is composed of two main components:

---

## 1) Server

The server listens on a specified port, accepting both **TCP connections** (used by subscribers) and **UDP messages** (used by publishers sending topic updates).

The server handles:

- TCP client connections, disconnections, and reconnections.  
- Each TCP client is uniquely identified by a **client ID**.
- Subscription management using the `"subscribe"` and `"unsubscribe"` commands.
- Persistent subscriptions: a client's subscriptions are preserved even if they disconnect, and restored upon reconnection.

To achieve this, a `map` is used to store the topics each client has subscribed to. This map remains unchanged unless the client unsubscribes or the server shuts down.

The server also supports **wildcard topics**:
- `*` matches multiple topic levels.
- `+` matches a single topic level.

When a UDP packet is received:
- The server parses the message and forwards it to **all currently connected TCP clients** subscribed to the matching topic (considering wildcard matching).
- The `"exit"` command shuts down the server.

---

## 2) Subscriber (TCP Client)

Subscribers are TCP clients that connect to the server using a unique ID.

- They send `"subscribe"` and `"unsubscribe"` commands (read from the keyboard) to manage their topic subscriptions.
- These commands are sent to the server, which updates both:
  - The **persistent subscriptions** (kept across reconnects)
  - The **active subscriptions** (for currently connected clients)

For every message received from a UDP client that matches a subscribed topic, the server sends the subscriber a message in the following format:


---

## Custom TCP Protocol

To ensure efficient and reliable communication between the server and TCP clients, a custom TCP protocol is used.

### 1) Message Framing

- Each message starts with a **4-byte length prefix** (big-endian using `htonl`/`ntohl`).
- This prefix represents the total size of the payload (i.e., `sizeof(TcpUdpMessage) + length of UDP content`).
- The client first reads the 4-byte prefix. Once the full length `L` is known, it reads exactly `L` bytes.
- If `recv()` returns less than `L`, the partial data is buffered. The client continues reading until `L` bytes are collected.
- If extra bytes are received, the surplus is buffered for the **next message**.
- This mechanism ensures correct message boundaries, regardless of how TCP fragments or coalesces packets.

### 2) Message Structure

Immediately after the 4-byte length prefix, the message has two parts:

#### a) Header (`TcpUdpMessage`)
A structure containing metadata:
- `udp_ip`: IP address of the source UDP client (in network byte order)
- `udp_port`: Port of the source UDP client
- `topic`: Topic string from the UDP message
- `data_type`: Data type of the content (`0=INT`, `1=SHORT_REAL`, `2=FLOAT`, `3=STRING`)

#### b) Payload (UDP Content)
- The raw content from the original UDP message, sent immediately after the header.
- The client computes the payload size as: `L - sizeof(TcpUdpMessage)`

---

## Performance Optimizations

- **Nagle’s algorithm** is disabled for both the server and the clients to minimize latency. This ensures small TCP packets are sent immediately, without waiting for more data to accumulate.
- The server sends messages **only** to clients that:
  - Are subscribed to the matching topic
  - Are currently connected

This is handled using:
- `persistent_subscriptions` – maps client IDs to their topic subscriptions
- `id_to_fd` – maps client IDs to their active socket descriptors

This custom TCP protocol ensures **message boundary integrity**, **efficient delivery**, and **low-latency communication** between the server and subscribers.
