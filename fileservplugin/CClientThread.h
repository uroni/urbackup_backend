#pragma warning ( disable:4005 )
#pragma warning ( disable:4996 )

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <WinBase.h>
#define MSG_NOSIGNAL 0
#else
#define HANDLE int
#endif

#include <deque>
#include <vector>
#include <queue>

#include "../Interface/Thread.h"
#include "../Interface/ThreadPool.h"
#include "bufmgr.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../common/data.h"
#include "types.h"
#include "settings.h"
#include "../md5.h"

class CTCPFileServ;
class IPipe;
class IFile;
class IMutex;
class ICondition;

#include "chunk_settings.h"
#include "packet_ids.h"

struct SSendData
{
	char* buffer;
	unsigned int bsize;

	bool last;

	bool delbuf;
	
	char* delbufptr;
};

struct SChunk
{
	SChunk()
		: msg(ID_ILLEGAL), update_file(NULL)
	{

	}

	explicit SChunk(char msg)
		: msg(msg), update_file(NULL)
	{

	}

	uchar msg;
	_i64 startpos;
	char transfer_all;
	char big_hash[big_hash_size];
	char small_hash[small_hash_size*(c_checkpoint_dist/c_small_hash_dist)];
	IFile* update_file;
	_i64 hashsize;
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

enum EClientState
{
	CS_NONE,
	CS_BLOCKHASH
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

	int SendInt(const char *buf, size_t bsize);
	bool getNextChunk(SChunk *chunk, bool has_error);
private:

	bool RecvMessage();
	bool ProcessPacket(CRData *data);
	void ReadFilePart(HANDLE hFile, const _i64 &offset,const bool &last);
	int SendData();
	void ReleaseMemory(void);
	void CloseThread(HANDLE hFile);

	void EnableNagle(void);
	void DisableNagle(void);

	bool GetFileBlockdiff(CRData *data);
	bool Handle_ID_BLOCK_REQUEST(CRData *data);

	void queueChunk(SChunk chunk);


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

	MD5 hash_func;
	_i64 next_checkpoint;
	_i64 sent_bytes;
	_i64 curr_filesize;

	IMutex *mutex;
	ICondition *cond;
	std::queue<SChunk> next_chunks;

	uchar cmd_id;

	EClientState state;
	THREADPOOL_TICKET chunk_send_thread_ticket;

	SOCKET int_socket;
	bool has_socket;
};
