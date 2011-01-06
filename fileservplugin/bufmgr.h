#include <vector>

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
	void releaseBuffer(char* buf);
	unsigned int nfreeBufffer(void);

private:

	std::vector<SBuffer> buffers;
	unsigned int freebufs;

};


