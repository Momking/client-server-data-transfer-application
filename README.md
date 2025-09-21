# UAP Client-Server

This project is a multithreaded and Non-blocking UDP client-server application that implements a custom protocol named UAP (UDP Application Protocol). The server is designed to handle multiple client sessions concurrently.

---

### Folder Structure
```
Kumar-Thakur-Lab3/
│
├── A/                      # Entry point for A
│ ├── client.cpp            # client
│ ├── client                # client bash file
│ ├── server.cpp            # server
│ └── server                # server bash file
├── B/
│ ├── client.cpp            # client
│ ├── client                # client bash file
│ ├── server.cpp            # server
│ ├── server                # server bash file
│ ├── pack.cpp              # packing of UAP header
│ └── unpack.cpp            # unPacking UAP header
├── include/
│ ├── UAP_header.h          # client
│ ├── pack.h                # client bash file
│ ├── unpack.h              # server
└──README.md
```

### How to Build & Run ▶️

**Their are two folders each containing a server and a client. First go inside any one of the folder and then proceed as follows :**

* **Start the Server**

Open a terminal and run the server, providing a port number to listen on.
```bash
./server 8080
```

* **Start the Client**

Open another terminal to run the client. Provide the server's IP address and port number. The client will then wait for input from the console.
```bash
./client 127.0.0.1 8080
```
* **Sending Data**

You can type messages directly into the client terminal. To send a file's content, use input redirection:
```bash
./client 127.0.0.1 8080 < input.txt
```