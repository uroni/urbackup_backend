#ifdef _WIN32
#	include <ws2tcpip.h>
#	include <windows.h>
#	define MSG_NOSIGNAL 0
#	define socklen_t _i32
#else
#	include <signal.h>
#	include <sys/socket.h>
#	include <sys/types.h>
#	include <poll.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#	include <unistd.h>
#	include <fcntl.h>
#	define SOCKET_ERROR -1
#	define closesocket close
#	define SOCKET int
#	define Sleep(x) usleep(x*1000)
#endif
#if defined(__sun__) || defined(__APPLE__)
#	define MSG_NOSIGNAL 0
#endif
#ifdef SOCK_CLOEXEC
#define ACCEPT_CLOEXEC(sockfd, addr, addrlen) accept4(sockfd, addr, addrlen, SOCK_CLOEXEC)
#else
#ifdef _WIN32
#define ACCEPT_CLOEXEC(sockfd, addr, addrlen) accept(sockfd, addr, addrlen)
#else
#ifndef ACCEPT_CLOEXEC_DEFINED
#define ACCEPT_CLOEXEC_DEFINED
namespace {
	int ACCEPT_CLOEXEC(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
		int rc = accept(sockfd, addr, addrlen);
		if(rc) fcntl(rc, F_SETFD, FD_CLOEXEC);
		return rc;
	}
}
#endif //ACCEPT_CLOEXEC_DEFINED
#endif //!_WIN32
#endif //!SOCK_CLOEXEC