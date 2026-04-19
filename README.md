# **Multithreaded TCP Server**

This is a simple **multithreaded TCP server** written in **C** that handles **HTTP GET requests**.  
It serves static files from the `www/` directory and supports **basic error handling**.

It is a multithreaded server that I built. Connecting through netcat or localhost a user can use GET to request different kinds of files and the server will respond with a header and the file content. The headers include status codes (ex 200, 404, 400...), content length, and content type.

---

## **Features**
✅ Handles multiple clients concurrently using **pthread**  
✅ Supports **GET requests** (no POST support)  
✅ Implements basic **HTTP/1.0 and HTTP/1.1**  
✅ **MIME type detection** for serving different file types  
✅ **Graceful shutdown** with `Ctrl+C` (SIGINT)  
✅ **Timeout handling** for inactive clients  

---

## **Compilation**
To compile the server, use:
```sh
gcc server.c -o server -lpthread -g
