#pragma once

#include "../Interface/Pipe.h"

#include <memory>
#include <vector>

// Reconstitui, em ordem, os frames recebidos por várias conexões TCP. O parser
// de ImageBackup continua enxergando exatamente o mesmo stream binário legado.
class ImageMultiPipeReader : public IPipe
{
public:
	explicit ImageMultiPipeReader(const std::vector<IPipe*>& pipes);
	~ImageMultiPipeReader();

	size_t Read(char* buffer, size_t bsize, int timeoutms = -1) override;
	bool Write(const char* buffer, size_t bsize, int timeoutms = -1, bool flush = true) override;
	size_t Read(std::string* ret, int timeoutms = -1) override;
	bool Write(const std::string& str, int timeoutms = -1, bool flush = true) override;
	bool Flush(int timeoutms = -1) override;
	bool isWritable(int timeoutms = 0) override;
	bool isReadable(int timeoutms = 0) override;
	bool hasError() override;
	void shutdown() override;
	size_t getNumElements() override;
	size_t getNumWaiters() override;
	void addThrottler(IPipeThrottler* throttler) override;
	void addOutgoingThrottler(IPipeThrottler* throttler) override;
	void addIncomingThrottler(IPipeThrottler* throttler) override;
	_i64 getTransferedBytes() override;
	void resetTransferedBytes() override;
	_i64 getRealTransferredBytes() override;
	void setUsageString(const std::string& str) override;
	bool setCompressionSettings(const SCompressionSettings& params) override;
	bool setOption(const SocketOption opt) override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
