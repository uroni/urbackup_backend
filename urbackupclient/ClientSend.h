#include "../Interface/Thread.h"
#include "../Interface/Pipe.h"

#include "../urbackupcommon/bufmgr.h"

#include <queue>

struct BufferItem
{
	char *buf;
	size_t bsize;
};

class ClientSend : public IThread
{
public:
	ClientSend(IPipe *pPipe, unsigned int bsize, unsigned int nbufs);
	~ClientSend(void);

	void operator()(void);

	char *getBuffer(void);
	std::vector<char*> getBuffers(unsigned int nbufs);
	void sendBuffer(char* buf, size_t bsize, bool do_notify);
	void notifySendBuffer(void);
	void freeBuffer(char *buf);

	void doExit(void);
	bool hasError(void);

	size_t getQueueSize(void);

private:
	void print_last_error(void);

	IPipe *pipe;

	CBufMgr2 *bufmgr;

	IMutex *mutex;
	ICondition *cond;
	std::queue<BufferItem> tqueue;

	volatile bool exit;
	volatile bool has_error;
};