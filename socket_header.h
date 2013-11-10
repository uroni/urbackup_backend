#ifdef _WIN32
#	include <windows.h>
#	define MSG_NOSIGNAL 0
#	define socklen_t _i32
#else
#	include <signal.h>
#	include <sys/socket.h>
#	include <sys/types.h>
#	include <sys/poll.h>
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
#ifdef sun
#	define MSG_NOSIGNAL 0
#endif