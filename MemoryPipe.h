#ifndef MEMPIPE_H_
#define MEMPIPE_H_

#include "Interface/Pipe.h"
#include <deque>
#include <string>
#include "Interface/Mutex.h"
#include "Interface/Condition.h"

class CMemoryPipe : public IPipe
{
public:
	CMemoryPipe(void);
	~CMemoryPipe(void);
	
	virtual size_t Read(char *buffer, size_t bsize, int timeoutms);
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms);
	virtual size_t Read(std::string *ret, int timeoutms);
	virtual bool Write(const std::string &str, int timeoutms);

	virtual bool isWritable(int timeoutms);
	virtual bool isReadable(int timeoutms);

	virtual bool hasError(void);

	virtual void shutdown(void);

	virtual size_t getNumElements(void);

	virtual void setThrottle(size_t bps);

	virtual size_t getTransferedBytes(void);
	virtual void resetTransferedBytes(void);
	
private:
	std::deque<std::string> queue;
	
	IMutex *mutex;
	ICondition *cond;
};

#endif /*MEMPIPE_H_*/
