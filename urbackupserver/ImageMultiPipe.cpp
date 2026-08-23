#include "ImageMultiPipe.h"

#include "../Interface/Server.h"
#include "../stringtools.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

namespace
{
	const _u32 frame_magic = 0x31534d48; // "HMS1" em little-endian
	const size_t frame_header_size = sizeof(_u32) + sizeof(uint64) + sizeof(_u32);
	const size_t max_frame_payload = 1024 * 1024;
	const size_t max_buffered_bytes = 64 * 1024 * 1024;

	bool readExact(IPipe* pipe, char* buffer, size_t size, const std::atomic<bool>& stopping)
	{
		size_t offset = 0;
		while (offset < size && !stopping.load())
		{
			const size_t read = pipe->Read(buffer + offset, size - offset, 1000);
			if (read == 0)
			{
				if (pipe->hasError()) return false;
				continue;
			}
			offset += read;
		}
		return offset == size;
	}
}

struct ImageMultiPipeReader::Impl
{
	std::vector<IPipe*> pipes;
	std::vector<std::thread> workers;
	std::mutex mutex;
	std::condition_variable changed;
	std::map<uint64, std::vector<char> > frames;
	std::atomic<bool> stopping;
	std::atomic<bool> error;
	uint64 next_sequence;
	size_t buffered_bytes;
	std::vector<char> current;
	size_t current_offset;

	explicit Impl(const std::vector<IPipe*>& p_pipes)
		: pipes(p_pipes), stopping(false), error(false), next_sequence(0),
		buffered_bytes(0), current_offset(0)
	{
		for (size_t i = 0; i < pipes.size(); ++i)
		{
			workers.push_back(std::thread(&Impl::runLane, this, i));
		}
	}

	~Impl()
	{
		stop();
		for (size_t i = 0; i < workers.size(); ++i)
		{
			if (workers[i].joinable()) workers[i].join();
		}
		for (size_t i = 0; i < pipes.size(); ++i)
		{
			Server->destroy(pipes[i]);
		}
	}

	void stop()
	{
		if (!stopping.exchange(true))
		{
			for (size_t i = 0; i < pipes.size(); ++i) pipes[i]->shutdown();
			changed.notify_all();
		}
	}

	void runLane(size_t lane)
	{
		IPipe* pipe = pipes[lane];
		while (!stopping.load())
		{
			char header[frame_header_size];
			if (!readExact(pipe, header, sizeof(header), stopping))
			{
				if (!stopping.load()) error.store(true);
				changed.notify_all();
				return;
			}

			_u32 magic;
			uint64 sequence;
			_u32 payload_size;
			memcpy(&magic, header, sizeof(magic));
			memcpy(&sequence, header + sizeof(magic), sizeof(sequence));
			memcpy(&payload_size, header + sizeof(magic) + sizeof(sequence), sizeof(payload_size));
			magic = little_endian(magic);
			sequence = little_endian(sequence);
			payload_size = little_endian(payload_size);
			if (magic != frame_magic || payload_size == 0 || payload_size > max_frame_payload)
			{
				error.store(true);
				changed.notify_all();
				return;
			}

			std::vector<char> payload(payload_size);
			if (!readExact(pipe, payload.data(), payload.size(), stopping))
			{
				if (!stopping.load()) error.store(true);
				changed.notify_all();
				return;
			}

			std::unique_lock<std::mutex> lock(mutex);
			if (sequence < next_sequence || frames.find(sequence) != frames.end())
			{
				error.store(true);
				changed.notify_all();
				return;
			}
			// Limita os frames fora de ordem. A lane que possui exatamente o
			// próximo frame sempre pode avançar, evitando deadlock quando outra
			// conexão fica mais lenta.
			while (!stopping.load() && !error.load()
				&& buffered_bytes + payload.size() > max_buffered_bytes
				&& sequence != next_sequence)
			{
				changed.wait(lock);
			}
			if (stopping.load() || error.load()) return;
			if (sequence < next_sequence || frames.find(sequence) != frames.end())
			{
				error.store(true);
				changed.notify_all();
				return;
			}
			buffered_bytes += payload.size();
			frames[sequence].swap(payload);
			changed.notify_all();
		}
	}

	size_t read(char* buffer, size_t bsize, int timeoutms)
	{
		if (bsize == 0) return 0;
		const std::chrono::steady_clock::time_point deadline = timeoutms < 0
			? std::chrono::steady_clock::time_point::max()
			: std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutms);
		size_t written = 0;
		std::unique_lock<std::mutex> lock(mutex);
		while (written < bsize)
		{
			if (current_offset < current.size())
			{
				const size_t amount = (std::min)(bsize - written, current.size() - current_offset);
				memcpy(buffer + written, current.data() + current_offset, amount);
				current_offset += amount;
				written += amount;
				if (current_offset == current.size())
				{
					current.clear();
					current_offset = 0;
				}
				continue;
			}

			std::map<uint64, std::vector<char> >::iterator frame = frames.find(next_sequence);
			if (frame != frames.end())
			{
				buffered_bytes -= frame->second.size();
				current.swap(frame->second);
				frames.erase(frame);
				++next_sequence;
				changed.notify_all();
				continue;
			}

			if (written != 0 || error.load() || stopping.load()) break;
			if (timeoutms < 0)
			{
				changed.wait(lock);
			}
			else if (changed.wait_until(lock, deadline) == std::cv_status::timeout)
			{
				break;
			}
		}
		return written;
	}

	bool readable(int timeoutms)
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (current_offset < current.size() || frames.find(next_sequence) != frames.end()) return true;
		if (timeoutms <= 0) return false;
		changed.wait_for(lock, std::chrono::milliseconds(timeoutms));
		return current_offset < current.size() || frames.find(next_sequence) != frames.end();
	}
};

ImageMultiPipeReader::ImageMultiPipeReader(const std::vector<IPipe*>& pipes)
	: impl(new Impl(pipes))
{
}

ImageMultiPipeReader::~ImageMultiPipeReader() = default;

size_t ImageMultiPipeReader::Read(char* buffer, size_t bsize, int timeoutms)
{
	return impl->read(buffer, bsize, timeoutms);
}

bool ImageMultiPipeReader::Write(const char* buffer, size_t bsize, int timeoutms, bool flush)
{
	return impl->pipes[0]->Write(buffer, bsize, timeoutms, flush);
}

size_t ImageMultiPipeReader::Read(std::string* ret, int timeoutms)
{
	char buffer[32768];
	const size_t read = impl->read(buffer, sizeof(buffer), timeoutms);
	ret->assign(buffer, buffer + read);
	return read;
}

bool ImageMultiPipeReader::Write(const std::string& str, int timeoutms, bool flush)
{
	return impl->pipes[0]->Write(str, timeoutms, flush);
}

bool ImageMultiPipeReader::Flush(int timeoutms)
{
	for (size_t i = 0; i < impl->pipes.size(); ++i)
	{
		if (!impl->pipes[i]->Flush(timeoutms)) return false;
	}
	return true;
}

bool ImageMultiPipeReader::isWritable(int timeoutms) { return impl->pipes[0]->isWritable(timeoutms); }
bool ImageMultiPipeReader::isReadable(int timeoutms) { return impl->readable(timeoutms); }
bool ImageMultiPipeReader::hasError() { return impl->error.load(); }
void ImageMultiPipeReader::shutdown() { impl->stop(); }
size_t ImageMultiPipeReader::getNumElements() { return 0; }
size_t ImageMultiPipeReader::getNumWaiters() { return 0; }
void ImageMultiPipeReader::addThrottler(IPipeThrottler* throttler) { for (size_t i = 0; i < impl->pipes.size(); ++i) impl->pipes[i]->addThrottler(throttler); }
void ImageMultiPipeReader::addOutgoingThrottler(IPipeThrottler* throttler) { for (size_t i = 0; i < impl->pipes.size(); ++i) impl->pipes[i]->addOutgoingThrottler(throttler); }
void ImageMultiPipeReader::addIncomingThrottler(IPipeThrottler* throttler) { for (size_t i = 0; i < impl->pipes.size(); ++i) impl->pipes[i]->addIncomingThrottler(throttler); }
_i64 ImageMultiPipeReader::getTransferedBytes()
{
	_i64 ret = 0;
	for (size_t i = 0; i < impl->pipes.size(); ++i) ret += impl->pipes[i]->getTransferedBytes();
	return ret;
}
void ImageMultiPipeReader::resetTransferedBytes() { for (size_t i = 0; i < impl->pipes.size(); ++i) impl->pipes[i]->resetTransferedBytes(); }
_i64 ImageMultiPipeReader::getRealTransferredBytes()
{
	_i64 ret = 0;
	for (size_t i = 0; i < impl->pipes.size(); ++i) ret += impl->pipes[i]->getRealTransferredBytes();
	return ret;
}

void ImageMultiPipeReader::setUsageString(const std::string& str)
{
	for (size_t i = 0; i < impl->pipes.size(); ++i) impl->pipes[i]->setUsageString(str);
}

bool ImageMultiPipeReader::setCompressionSettings(const SCompressionSettings& params)
{
	bool ok = true;
	for (size_t i = 0; i < impl->pipes.size(); ++i)
	{
		if (!impl->pipes[i]->setCompressionSettings(params)) ok = false;
	}
	return ok;
}

bool ImageMultiPipeReader::setOption(const SocketOption opt)
{
	bool ok = true;
	for (size_t i = 0; i < impl->pipes.size(); ++i)
	{
		if (!impl->pipes[i]->setOption(opt)) ok = false;
	}
	return ok;
}
