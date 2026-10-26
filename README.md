# TFTP Client-Server File Transfer in C

## Project Description
A Trivial File Transfer Protocol (TFTP) client-server application developed in C using UDP socket programming. The project supports file uploads and downloads between a client and server, implementing packet exchange, acknowledgments, timeout handling, retransmissions, and error management.

## Features
- Connect to a TFTP server using its IP address.
- Upload files using the `put` command.
- Download files using the `get` command.
- Support **Octet** and **Netascii** transfer modes.
- Handle TFTP request, data, acknowledgment, and error packets.
- Implement timeout and retransmission mechanisms.
- Validate filenames and handle file access errors.
- Display transfer progress.
- Support interactive client commands and connection management.

## Technologies Used
- **Language:** C
- **Platform:** Linux
- **Compiler:** GCC
- **Networking:** UDP Sockets
- **Protocol:** TFTP (RFC 1350)
- **Concepts:** Client-Server Architecture, Socket Programming, File Handling, Network Byte Order, Error Handling, Timeouts and Retransmissions

## Project Structure
- `tftp_client.c` – Client interface and file transfer commands.
- `tftp_client.h` – Client structures and function declarations.
- `tftp_server.c` – Server-side request handling and file transfer.
- `tftp.c` – Common packet processing, file transfer, and Netascii conversion functions.
- `tftp.h` – Shared protocol definitions, packet structures, and function declarations.
- `makefile` – Build and clean commands.
- `client_test/`, `get_test/`, `server_test/` – Test directories and supporting files.

## Compilation
Compile both the client and server using:

```bash
make
```

To remove generated executables:

```bash
make clean
```

## Execution

### 1. Start the Server
Open a terminal in the project directory:

```bash
./tftp_server
```

### 2. Start the Client
Open another terminal:

```bash
./tftp_client
```

Or connect directly using the server IP:

```bash
./tftp_client 127.0.0.1
```

### 3. Client Commands

| Command | Description |
|---|---|
| `connect <server-ip>` | Connect to the server |
| `put <file-name>` | Upload a file |
| `get <file-name>` | Download a file |
| `mode` | Select transfer mode |
| `mode octet` | Select binary transfer mode |
| `mode netascii` | Select text transfer mode |
| `disconnect` | Disconnect from the server |
| `help` | Display supported commands |
| `bye` / `quit` | Exit the client |

## Protocol Operations
The implementation uses the standard TFTP packet types:

- **RRQ:** Read Request
- **WRQ:** Write Request
- **DATA:** Data packet
- **ACK:** Acknowledgment
- **ERROR:** Error response

File data is transferred in blocks of up to 512 bytes, with a 4-byte TFTP header.

## Key Learnings
- Implementing UDP client-server communication in C.
- Understanding TFTP packet formats and protocol flow.
- Managing file transfers using DATA and ACK packets.
- Handling timeouts, retransmissions, and communication errors.
- Converting text files between Netascii and local representations.
- Building modular C applications using a Makefile.

## Author
Soundharya Lahari Uppula
