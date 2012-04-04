#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../Interface/Pipe.h"

class BackupServerPrepareHash : public IThread
{
public:
	BackupServerPrepareHash(IPipe *pPipe, IPipe *pExitpipe, IPipe *pOutput, IPipe *pExitpipe_hash, int pClientid);
	~BackupServerPrepareHash(void);

	void operator()(void);
	
	bool isWorking(void);

	static std::string build_chunk_hashs(IFile *f, IFile *hashoutput);

private:
	std::string hash(IFile *f);

	IPipe *pipe;
	IPipe *exitpipe;
	IPipe *output;
	IPipe *exitpipe_hash;

	int clientid;
	
	volatile bool working;
};