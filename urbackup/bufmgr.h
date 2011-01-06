#include <vector>
#include <stack>

#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/File.h"

struct SBuffer
{
	char* buffer;
	bool used;
};

class CBufMgr
{
public:
	CBufMgr(unsigned int nbuf, unsigned int bsize);
	~CBufMgr(void);

	char* getBuffer(void);
	std::vector<char*> getBuffers(unsigned int n);
	void releaseBuffer(char* buf);
	unsigned int nfreeBufffer(void);

private:

	std::vector<SBuffer> buffers;
	unsigned int freebufs;

	IMutex *mutex;
	ICondition *cond;

};

class CBufMgr2
{
public:
	CBufMgr2(unsigned int nbuf, unsigned int bsize);
	~CBufMgr2(void);

	char* getBuffer(void);
	std::vector<char*> getBuffers(unsigned int n);
	void releaseBuffer(char* buf);
	unsigned int nfreeBufffer(void);

private:

	std::stack<char*> free_bufs;
	char *bufptr;

	IMutex *mutex;
	ICondition *cond;

};

class CFileBufMgr
{
public:
	CFileBufMgr(unsigned int nbuf, bool pMemory);
	~CFileBufMgr(void);

	IFile* getBuffer(void);
	std::vector<IFile*> getBuffers(unsigned int n);
	void releaseBuffer(IFile *buf);
	unsigned int nfreeBuffer;

private:
	unsigned int nbufs;
	bool memory;
	IMutex *mutex;
	ICondition *cond;
};


