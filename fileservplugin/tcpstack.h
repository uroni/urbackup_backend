#include <vector>


#include "socket_header.h"

#define MAX_PACKETSIZE	unsigned int

class CWData;

class CTCPStack
{
public:
	void AddData(char* buf, size_t datasize);

	char* getPacket(size_t* packsize);

	int Send(SOCKET sock, char* buf, size_t msglen);
	int Send(SOCKET sock, CWData data);

        void reset(void);

private:
	
	std::vector<char> buffer;
};

