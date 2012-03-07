#pragma warning ( disable:4005 )
#pragma warning ( disable:4996 )

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <WinBase.h>
#define MSG_NOSIGNAL 0
#endif

#include <deque>
#include <vector>

#include "../Interface/Thread.h"
#include "bufmgr.h"
#include "tcpstack.h"
#include "data.h"
#include "types.h"
#include "settings.h"

class CTCPFileServ;
class IPipe;

struct SSendData
{
	char* buffer;
	unsigned int bsize;

	bool last;

	bool delbuf;
	char* delbufptr;
};

struct SLPData
{
	std::deque<SSendData*> *t_send;
	std::vector<SLPData*> *t_unsend;
	char* buffer;
	bool last;

	int filepart;
	int *sendfilepart;

	unsigned int bsize;
};

class CClientThread : public IThread
{
public:
	CClientThread(SOCKET pSocket, CTCPFileServ* pParent);
	CClientThread(IPipe *pClientpipe, CTCPFileServ* pParent);
	~CClientThread();

	bool isStopped(void);
	bool isKillable(void);
	
	void operator()(void);

	void StopThread(void);
private:

	bool RecvMessage(void);
	bool ProcessPacket(CRData *data);
	void ReadFilePart(HANDLE hFile, const _i64 &offset,const bool &last);
	int SendData(void);
	void ReleaseMemory(void);
	void CloseThread(HANDLE hFile);

	void EnableNagle(void);
	void DisableNagle(void);

	int SendInt(const char *buf, size_t bsize);

	SOCKET mSocket;
	volatile bool stopped;
	volatile bool killable;

	int currfilepart;
	int sendfilepart;

	CBufMgr* bufmgr;
	CTCPStack stack;
	char buffer[BUFFERSIZE];

	std::deque<SSendData*> t_send;
	std::vector<SLPData*> t_unsend;

	HANDLE hFile;

	int errcount;

	bool close_the_socket;

	CTCPFileServ *parent;
	IPipe *clientpipe;
};
