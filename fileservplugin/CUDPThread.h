#pragma warning ( disable:4005 )
#pragma warning ( disable:4996 )

#include "../Interface/Thread.h"
#include "types.h"
#include "socket_header.h"
#include <string>


class CUDPThread : public IThread
{
public:
	CUDPThread(_u16 udpport, std::string servername);
	~CUDPThread();

	std::string getServername();

	void operator()(void);
private:
	SOCKET udpsock;
	std::string mServername;

	bool UdpStep(void);
};

