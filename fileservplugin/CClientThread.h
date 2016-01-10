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
class IFsFile;
class IMutex;
class ICondition;
class ScopedPipeFileUser;

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
		: msg(ID_ILLEGAL), update_file(NULL), pipe_file_user(NULL)
	{

	}

	explicit SChunk(char msg)
		: msg(msg), update_file(NULL), pipe_file_user(NULL)
	{

	}

	uchar msg;
	_i64 startpos;
	char transfer_all;
	char big_hash[big_hash_size];
	char small_hash[small_hash_size*(c_checkpoint_dist/c_small_hash_dist)];
	IFile* update_file;
	_i64 hashsize;
	int64 requested_filesize;
	ScopedPipeFileUser* pipe_file_user;
	bool with_sparse;
};

struct SLPData
{
	std::deque<SSendData*> *t_send;
	std::vector<SLPData*> *t_unsend;
	unsigned int *errorcode;
	char* buffer;
	bool last;

	int filepart;
	int *sendfilepart;

	bool has_error;

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
	CClientThread(IPipe *pClientpipe, CTCPFileServ* pParent, std::vector<char>* extra_buffer);
	~CClientThread();

	bool isStopped(void);
	bool isKillable(void);
	
	void operator()(void);

	void StopThread(void);

    int SendInt(const char *buf, size_t bsize, bool flush=false);
	bool FlushInt();
	bool getNextChunk(SChunk *chunk, bool has_error);
private:

	bool sendFullFile(IFile* file, _i64 start_offset, bool with_hashes);

	bool RecvMessage();
	bool ProcessPacket(CRData *data);
	bool ReadFilePart(HANDLE hFile, _i64 offset, bool last, _u32 toread);
	int SendData();
	void ReleaseMemory(void);
	void CloseThread(HANDLE hFile);

	bool GetFileBlockdiff(CRData *data, bool with_metadata);
	bool Handle_ID_BLOCK_REQUEST(CRData *data);

	bool GetFileHashAndMetadata(CRData* data);

	void queueChunk(SChunk chunk);
	bool InformMetadataStreamEnd( CRData * data );
	bool FinishScript( CRData * data );

	int64 getFileExtents(int64 fsize, int64& n_sparse_extents);

	bool sendSparseExtents(int64 fsize, int64 n_sparse_extents);

	volatile bool stopped;
	volatile bool killable;

	int currfilepart;
	int sendfilepart;

	fileserv::CBufMgr* bufmgr;
	CTCPStack stack;
	char buffer[BUFFERSIZE];

	std::deque<SSendData*> t_send;
	std::vector<SLPData*> t_unsend;
	unsigned int errorcode;

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

	bool with_hashes;

	EClientState state;
	THREADPOOL_TICKET chunk_send_thread_ticket;

	SOCKET int_socket;
	bool has_socket;

	std::vector<char>* extra_buffer;

	struct SExtent
	{
		SExtent()
			: offset(-1), size(-1)
		{

		}

		SExtent(int64 offset, int64 size)
			: offset(offset), size(size)
		{}

		bool operator<(const SExtent& other) const
		{
			return offset < other.offset;
		}

		int64 offset;
		int64 size;
	};

	bool has_file_extents;
	std::vector<SExtent> file_extents;
};
