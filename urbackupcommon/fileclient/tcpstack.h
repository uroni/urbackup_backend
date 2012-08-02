#ifndef TCPSTACK_H
#define TCPSTACK_H

#include <vector>

#define MAX_PACKETSIZE	unsigned int

#include "../../Interface/Pipe.h"

class CWData;

class CTCPStack
{
public:
	CTCPStack(bool add_checksum=false);
	void AddData(char* buf, size_t datasize);

	char* getPacket(size_t* packsize);

	size_t Send(IPipe* p, char* buf, size_t msglen);
	size_t Send(IPipe* p, CWData data);
	size_t Send(IPipe* p, const std::string &msg);

    void reset(void);

	char *getBuffer();
	size_t getBuffersize();

	void setAddChecksum(bool b);

private:
	
	std::vector<char> buffer;

	bool add_checksum;
};

#endif //TCPSTACK_H