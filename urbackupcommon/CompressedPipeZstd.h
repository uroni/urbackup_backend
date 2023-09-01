#pragma once

#ifndef NO_ZSTD_COMPRESSION

#include "../Interface/Pipe.h"
#include "../Interface/Types.h"
#include "CompressedPipe2.h"
#include <vector>
#include <memory>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

class IMutex;

class CompressedPipeZstd : public ICompressedPipe
{
public:
	CompressedPipeZstd(IPipe *cs, int compression_level, int threads);
	~CompressedPipeZstd(void);

	virtual size_t Read(char *buffer, size_t bsize, int timeoutms=-1);
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms=-1, bool flush=true);
	virtual size_t Read(std::string *ret, int timeoutms=-1);
	virtual bool Write(const std::string &str, int timeoutms=-1, bool flush=true);

	/**
	* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
	*/
	virtual bool isWritable(int timeoutms=0);
	virtual bool isReadable(int timeoutms=0);

	virtual bool hasError(void);

	virtual void shutdown(void);

	virtual size_t getNumWaiters();;

	virtual size_t getNumElements(void);

	virtual void destroyBackendPipeOnDelete(bool b);

	virtual IPipe *getRealPipe(void);

	virtual void addThrottler(IPipeThrottler *throttler);
	virtual void addOutgoingThrottler(IPipeThrottler *throttler);
	virtual void addIncomingThrottler(IPipeThrottler *throttler);

	virtual _i64 getTransferedBytes(void);
	virtual void resetTransferedBytes(void);

	virtual bool Flush( int timeoutms=-1 );

	int64 getUncompressedSentBytes();
	int64 getUncompressedReceivedBytes();
	int64 getSentFlushes();

	virtual _i64 getRealTransferredBytes();

	virtual void setUsageString(const std::string& str);

	virtual bool setCompressionSettings(const SCompressionSettings& params);

private:
	virtual bool WriteInt(const char *buffer, size_t bsize, int timeoutms = -1, ZSTD_EndDirective flush= ZSTD_e_continue);

	size_t ProcessToBuffer(char *buffer, size_t bsize, bool fromLast);
	void ProcessToString(std::string* ret, bool fromLast);

	IPipe *cs;
	char* comp_buffer;
	size_t comp_buffer_size;
	std::vector<char> input_buffer;
	size_t input_buffer_size;

	int64 uncompressed_sent_bytes;
	int64 uncompressed_received_bytes;
	int64 sent_flushes;
	int64 last_send_time;

	struct ZstdAdaptive
	{
		int64 flush_timeout;
		ZSTD_frameProgression zfp_prev;
		ZSTD_frameProgression zfp_prev_corr;
		size_t n_zstd_workers;
		size_t zstd_input_blocked;
		size_t zstd_input_presented;
		int comp_level;
		unsigned int last_job_id;
	};

	std::auto_ptr<ZstdAdaptive> adaptive;

	bool destroy_cs;
	bool has_error;
	
	ZSTD_DStream* inf_stream;
	ZSTD_CCtx* def_stream;
	ZSTD_inBuffer inf_in_last;

	std::auto_ptr<IMutex> read_mutex;
	std::auto_ptr<IMutex> write_mutex;

	std::auto_ptr<IMutex> usage_mutex;
	std::string usage_curr;
	std::string usage_add;
};

#endif //NO_ZSTD_COMPRESSION
