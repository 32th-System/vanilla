#ifndef VANILLA_OS_H
#define VANILLA_OS_H

// Note about any explicit conversions to void*:
// The POSIX socket API defines all buffer parameters to be void*. Winsock2 defines them to be to be char*.
// C allows implicit conversions between void* and any T*, it does NOT however allow implicit conversions
// between different non-void pointer types. By explicity casting to void*, any implicit conversion to
// any other pointer type is guaranteed to work.
// When using send, recv, sendto, recvfrom, etc..., remember to cast to void*

#if defined(__unix__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
typedef int socket_t;
#define vanilla_close_socket(sock) (close(sock))
#define vanilla_sleep(ms) (usleep(ms * 1000))
#define vanilla_socket_error() (errno)
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET socket_t;
//typedef int ssize_t;
typedef u_short in_port_t;
#define vanilla_close_socket(sock) closesocket(sock)
#define vanilla_socket_error() (WSAGetLastError())

// Windows.h MUST be included AFTER the winsock headers
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOMINMAX
#include <Windows.h>
#define vanilla_sleep Sleep

#endif
#endif