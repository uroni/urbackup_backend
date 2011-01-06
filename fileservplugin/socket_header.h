#ifdef _WIN32
#	include <windows.h>
#	define MSG_NOSIGNAL 0
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
#	include <sys/sendfile.h>
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
