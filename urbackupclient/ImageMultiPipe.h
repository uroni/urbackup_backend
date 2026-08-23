#pragma once

#include "../Interface/Pipe.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace image_multi
{
	const int max_streams = 8;

	bool createSession(const std::string& stream_id, const std::string& server_token,
		int expected_streams);
	bool addSessionPipe(const std::string& stream_id, const std::string& server_token,
		int lane, IPipe* pipe);
	void cancelSession(const std::string& stream_id, const std::string& server_token);
	std::vector<IPipe*> acquireSessionPipes(const std::string& stream_id,
		const std::string& server_token, int expected_streams, int timeout_ms);

	// Recebe o fluxo lógico produzido pelo ImageThread e o distribui em frames
	// numerados. Cada lane possui uma fila e uma thread de escrita próprias; assim
	// um socket aguardando ACK/TCP window não bloqueia os demais.
	class Writer : public IPipe
	{
	public:
		Writer(IPipe* primary, const std::vector<IPipe*>& additional_pipes);
		~Writer();

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
}
