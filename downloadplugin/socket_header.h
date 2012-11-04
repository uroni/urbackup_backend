#ifndef SOCKET_HEADER_H
#define SOCKET_HEADER_H

#ifdef _WIN32
#	include <winsock2.h>
#	include <windows.h>
#ifndef MSG_NOSIGNAL
#	define MSG_NOSIGNAL 0
#endif
#	define socklen_t _i32
#else
#	include <signal.h>
#	include <sys/socket.h>
#	include <sys/types.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#	include <unistd.h>
#	include <fcntl.h>
#	include <errno.h>
#	define SOCKET_ERROR -1
#	define closesocket close
#	define SOCKET int
#	define BOOL bool
#	define CloseHandle close
#	define HANDLE int
#	define FALSE false
#	define TRUE true
# 	define GetTickCount() (unsigned int)(clock()/CLOCKS_PER_SEC)
#	define INVALID_HANDLE_VALUE -1
#	define Sleep(x) usleep(x*1000)
#endif
#ifdef sun
#	define MSG_NOSIGNAL 0
#endif

#endif //SOCKET_HEADER_H