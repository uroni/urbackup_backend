#pragma once

#include "../Interface/Pipe.h"
#include "../Interface/Mutex.h"
#include <memory>

class WebSocketPipe : public IPipe
{
	enum EReadState
	{
		EReadState_Header1,
		EReadState_HeaderSize1,
		EReadState_HeaderSize2,
		EReadState_HeaderMask,
		EReadState_Body
	};

public:
	WebSocketPipe(IPipe* pipe, bool mask_writes, bool expect_read_mask, std::string pipe_add, bool destroy_pipe);
	~WebSocketPipe();

	virtual size_t Read(char* buffer, size_t bsize, int timeoutms = -1);

	virtual bool Write(const char* buffer, size_t bsize, int timeoutms = -1, bool flush = true);

	virtual size_t Read(std::string* ret, int timeoutms = -1);

	virtual bool Write(const std::string& str, int timeoutms = -1, bool flush = true)
	{
		return Write(str.data(), str.size(), timeoutms, flush);
	}
	virtual bool Flush(int timeoutms = -1)
	{
		return pipe->Flush(timeoutms);
	}
	virtual bool isWritable(int timeoutms = 0)
	{
		return pipe->isWritable();
	}
	virtual bool isReadable(int timeoutms = 0)
	{
		return pipe->isReadable();
	}
	virtual bool hasError(void)
	{
		return has_error || pipe->hasError();
	}
	virtual void shutdown(void)
	{
		pipe->shutdown();
	}
	virtual size_t getNumElements(void)
	{
		return pipe->getNumElements();
	}
	virtual void addThrottler(IPipeThrottler* throttler)
	{
		pipe->addThrottler(throttler);
	}
	virtual void addOutgoingThrottler(IPipeThrottler* throttler)
	{
		pipe->addOutgoingThrottler(throttler);
	}
	virtual void addIncomingThrottler(IPipeThrottler* throttler)
	{
		pipe->addIncomingThrottler(throttler);
	}
	virtual _i64 getTransferedBytes(void)
	{
		return pipe->getTransferedBytes();
	}
	virtual void resetTransferedBytes(void)
	{
		pipe->resetTransferedBytes();
	}

private:

	bool has_read_mask()
	{
		return header_bits2 & (1 << 7);
	}

	unsigned char get_opcode()
	{
		return (header_bits1 & (1 << 3 | 1 << 2 | 1 << 1 | 1 << 0));
	}

	size_t consume(char* buffer, size_t bsize, int write_timeoutms, size_t* consumed_out);

	bool mask_writes;
	bool expect_read_mask;
	IPipe* pipe;

	EReadState read_state;
	std::vector<char> read_buffer;
	unsigned char header_bits1;
	unsigned char header_bits2;
	uint64 payload_size;
	size_t remaining_size_bytes;
	size_t consumed_size_bytes;
	unsigned int read_mask;
	unsigned int read_mask_idx;
	bool has_error;
	std::string pipe_add;
	unsigned int masking_key;
	bool destroy_pipe;

	std::auto_ptr<IMutex> read_mutex;
	std::auto_ptr<IMutex> write_mutex;
};