# Simple-HTTP-1.0-Server

## Description
A basic Linux HTTP server that serves a requested file or directory.

The purpose of this project was for me to learn C, HTTP and Network Programming basics.

Great resource which I recommend checking out: [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)

## Usage
Compiling binary:

```sh
make all
```

Binary usage:

```sh
./http_server <port> <directory>
```

Example:

```sh
./http_server 8080 /home/user
```
