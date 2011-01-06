#ifndef TCPSTACK_H
#define TCPSTACK_H

#include <vector>

#define MAX_PACKETSIZE	unsigned int

#include "../../Interface/Pipe.h"

class CWData;

class CTCPStack
{
public:
	void AddData(char* buf, size_t datasize);

	char* getPacket(size_t* packsize);

	size_t Send(IPipe* p, char* buf, size_t msglen);
	size_t Send(IPipe* p, CWData data);
	size_t Send(IPipe* p, const std::string &msg);

    void reset(void);

	char *getBuffer();
	size_t getBuffersize();

private:
	
	std::vector<char> buffer;
};

#endif //TCPSTACK_H