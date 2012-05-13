#include "../Interface/Pipe.h"
#include "../Interface/Types.h"
#include <vector>

class IZlibCompression;
class IZlibDecompression;


enum RecvState
{
	RS_LENGTH,
	RS_CONTENT
};

class CompressedPipe : public IPipe
{
public:
	CompressedPipe(IPipe *cs, int compression_level);
	~CompressedPipe(void);

	virtual size_t Read(char *buffer, size_t bsize, int timeoutms=-1);
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms=-1);
	virtual size_t Read(std::string *ret, int timeoutms=-1);
	virtual bool Write(const std::string &str, int timeoutms=-1);

	/**
	* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
	*/
	virtual bool isWritable(int timeoutms=0);
	virtual bool isReadable(int timeoutms=0);

	virtual bool hasError(void);

	virtual void shutdown(void);

	virtual size_t getNumElements(void);

	void destroyBackendPipeOnDelete(bool b);

	IPipe *getRealPipe(void);

	virtual void addThrottler(IPipeThrottler *throttler);
	virtual void addOutgoingThrottler(IPipeThrottler *throttler);
	virtual void addIncomingThrottler(IPipeThrottler *throttler);

	virtual _i64 getTransferedBytes(void);
	virtual void resetTransferedBytes(void);

private:
	void Process(const char *buffer, size_t bsize);
	size_t ReadToBuffer(char *buffer, size_t bsize);
	size_t ReadToString(std::string *ret);

	IPipe *cs;

	IZlibCompression *comp;
	IZlibDecompression *decomp;

	std::vector<char> decomp_buffer;
	size_t decomp_buffer_pos;
	size_t decomp_read_pos;
	std::vector<char> comp_buffer;
	std::vector<char> input_buffer;
	size_t input_buffer_pos;

	int recv_state;
	_u16 message_len;
	size_t message_left;
	size_t message_len_byte;

	bool destroy_cs;
};