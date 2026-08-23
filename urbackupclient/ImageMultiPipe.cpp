#include "ImageMultiPipe.h"

#include "../Interface/Server.h"
#include "../stringtools.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace image_multi
{
	namespace
	{
		const _u32 frame_magic = 0x31534d48; // "HMS1" em little-endian
		const size_t frame_header_size = sizeof(_u32) + sizeof(uint64) + sizeof(_u32);
		const size_t frame_payload_size = 256 * 1024;
		const size_t max_queued_bytes = 64 * 1024 * 1024;

		struct Session
		{
			std::string token;
			int expected_streams;
			std::map<int, IPipe*> pipes;
			std::chrono::steady_clock::time_point created;
		};

		std::mutex sessions_mutex;
		std::condition_variable sessions_changed;
		std::map<std::string, Session> sessions;

		void destroyPipes(std::map<int, IPipe*>& pipes)
		{
			for (std::map<int, IPipe*>::iterator it = pipes.begin(); it != pipes.end(); ++it)
			{
				if (it->second != nullptr)
				{
					it->second->shutdown();
					Server->destroy(it->second);
				}
			}
			pipes.clear();
		}

		void removeExpiredSessions()
		{
			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
			for (std::map<std::string, Session>::iterator it = sessions.begin(); it != sessions.end();)
			{
				if (now - it->second.created > std::chrono::minutes(2))
				{
					destroyPipes(it->second.pipes);
					it = sessions.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		struct Frame
		{
			uint64 sequence;
			std::vector<char> payload;
		};
	}

	bool createSession(const std::string& stream_id, const std::string& server_token,
		int expected_streams)
	{
		if (stream_id.empty() || server_token.empty()
			|| expected_streams < 2 || expected_streams > max_streams)
		{
			return false;
		}

		std::lock_guard<std::mutex> lock(sessions_mutex);
		removeExpiredSessions();
		std::map<std::string, Session>::iterator old = sessions.find(stream_id);
		if (old != sessions.end())
		{
			destroyPipes(old->second.pipes);
			sessions.erase(old);
		}
		Session session;
		session.token = server_token;
		session.expected_streams = expected_streams;
		session.created = std::chrono::steady_clock::now();
		sessions[stream_id] = session;
		return true;
	}

	bool addSessionPipe(const std::string& stream_id, const std::string& server_token,
		int lane, IPipe* pipe)
	{
		if (pipe == nullptr)
		{
			return false;
		}

		std::lock_guard<std::mutex> lock(sessions_mutex);
		std::map<std::string, Session>::iterator it = sessions.find(stream_id);
		if (it == sessions.end() || it->second.token != server_token
			|| lane < 1 || lane >= it->second.expected_streams
			|| it->second.pipes.find(lane) != it->second.pipes.end())
		{
			return false;
		}
		it->second.pipes[lane] = pipe;
		sessions_changed.notify_all();
		return true;
	}

	void cancelSession(const std::string& stream_id, const std::string& server_token)
	{
		std::lock_guard<std::mutex> lock(sessions_mutex);
		std::map<std::string, Session>::iterator it = sessions.find(stream_id);
		if (it != sessions.end() && it->second.token == server_token)
		{
			destroyPipes(it->second.pipes);
			sessions.erase(it);
			sessions_changed.notify_all();
		}
	}

	std::vector<IPipe*> acquireSessionPipes(const std::string& stream_id,
		const std::string& server_token, int expected_streams, int timeout_ms)
	{
		std::vector<IPipe*> ret;
		std::unique_lock<std::mutex> lock(sessions_mutex);
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

		while (true)
		{
			std::map<std::string, Session>::iterator it = sessions.find(stream_id);
			if (it == sessions.end() || it->second.token != server_token
				|| it->second.expected_streams != expected_streams)
			{
				return ret;
			}
			if (static_cast<int>(it->second.pipes.size()) == expected_streams - 1)
			{
				for (int lane = 1; lane < expected_streams; ++lane)
				{
					ret.push_back(it->second.pipes[lane]);
				}
				it->second.pipes.clear();
				sessions.erase(it);
				return ret;
			}
			if (sessions_changed.wait_until(lock, deadline) == std::cv_status::timeout)
			{
				destroyPipes(it->second.pipes);
				sessions.erase(it);
				return ret;
			}
		}
	}

	struct Writer::Impl
	{
		struct Lane
		{
			IPipe* pipe;
			bool owned;
			std::deque<std::shared_ptr<Frame> > queue;
			std::thread worker;
		};

		std::vector<std::unique_ptr<Lane> > lanes;
		std::mutex mutex;
		std::condition_variable changed;
		std::atomic<bool> error;
		bool stopping;
		uint64 next_sequence;
		size_t queued_bytes;
		size_t active_writes;

		Impl(IPipe* primary, const std::vector<IPipe*>& additional_pipes)
			: error(false), stopping(false), next_sequence(0), queued_bytes(0), active_writes(0)
		{
			std::unique_ptr<Lane> primary_lane(new Lane());
			primary_lane->pipe = primary;
			primary_lane->owned = false;
			lanes.push_back(std::move(primary_lane));
			for (size_t i = 0; i < additional_pipes.size(); ++i)
			{
				std::unique_ptr<Lane> lane(new Lane());
				lane->pipe = additional_pipes[i];
				lane->owned = true;
				lanes.push_back(std::move(lane));
			}
			for (size_t i = 0; i < lanes.size(); ++i)
			{
				lanes[i]->worker = std::thread(&Impl::runLane, this, i);
			}
		}

		~Impl()
		{
			flush(-1);
			{
				std::lock_guard<std::mutex> lock(mutex);
				stopping = true;
				changed.notify_all();
			}
			if (error.load())
			{
				for (size_t i = 0; i < lanes.size(); ++i)
				{
					lanes[i]->pipe->shutdown();
				}
			}
			for (size_t i = 0; i < lanes.size(); ++i)
			{
				if (lanes[i]->worker.joinable())
				{
					lanes[i]->worker.join();
				}
				if (lanes[i]->owned)
				{
					Server->destroy(lanes[i]->pipe);
				}
			}
		}

		void runLane(size_t lane_idx)
		{
			Lane& lane = *lanes[lane_idx];
			while (true)
			{
				std::shared_ptr<Frame> frame;
				{
					std::unique_lock<std::mutex> lock(mutex);
					changed.wait(lock, [&]() { return stopping || error.load() || !lane.queue.empty(); });
					if ((stopping || error.load()) && lane.queue.empty())
					{
						return;
					}
					frame = lane.queue.front();
					lane.queue.pop_front();
					++active_writes;
				}

				char header[frame_header_size];
				_u32 magic = little_endian(frame_magic);
				uint64 sequence = little_endian(frame->sequence);
				_u32 payload_size = little_endian(static_cast<_u32>(frame->payload.size()));
				memcpy(header, &magic, sizeof(magic));
				memcpy(header + sizeof(magic), &sequence, sizeof(sequence));
				memcpy(header + sizeof(magic) + sizeof(sequence), &payload_size, sizeof(payload_size));

				bool ok = lane.pipe->Write(header, sizeof(header), 60000, false)
					&& lane.pipe->Write(frame->payload.data(), frame->payload.size(), 60000, false);
				{
					std::lock_guard<std::mutex> lock(mutex);
					queued_bytes -= frame->payload.size();
					--active_writes;
					if (!ok)
					{
						error.store(true);
					}
					changed.notify_all();
				}
			}
		}

		bool write(const char* buffer, size_t bsize, int timeoutms, bool do_flush)
		{
			if (lanes.empty() || error.load())
			{
				return false;
			}

			const std::chrono::steady_clock::time_point deadline = timeoutms < 0
				? std::chrono::steady_clock::time_point::max()
				: std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutms);
			size_t offset = 0;
			while (offset < bsize)
			{
				const size_t amount = (std::min)(frame_payload_size, bsize - offset);
				std::shared_ptr<Frame> frame(new Frame());
				frame->payload.assign(buffer + offset, buffer + offset + amount);
				std::unique_lock<std::mutex> lock(mutex);
				while (!error.load() && queued_bytes + amount > max_queued_bytes)
				{
					if (timeoutms < 0)
					{
						changed.wait(lock);
					}
					else if (changed.wait_until(lock, deadline) == std::cv_status::timeout)
					{
						return false;
					}
				}
				if (error.load())
				{
					return false;
				}
				frame->sequence = next_sequence++;
				Lane& lane = *lanes[static_cast<size_t>(frame->sequence % lanes.size())];
				lane.queue.push_back(frame);
				queued_bytes += amount;
				offset += amount;
				changed.notify_all();
			}
			return !do_flush || flush(timeoutms);
		}

		bool flush(int timeoutms)
		{
			const std::chrono::steady_clock::time_point deadline = timeoutms < 0
				? std::chrono::steady_clock::time_point::max()
				: std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutms);
			std::unique_lock<std::mutex> lock(mutex);
			while (!error.load() && (queued_bytes != 0 || active_writes != 0))
			{
				if (timeoutms < 0)
				{
					changed.wait(lock);
				}
				else if (changed.wait_until(lock, deadline) == std::cv_status::timeout)
				{
					return false;
				}
			}
			if (error.load())
			{
				return false;
			}
			lock.unlock();
			for (size_t i = 0; i < lanes.size(); ++i)
			{
				if (!lanes[i]->pipe->Flush(timeoutms))
				{
					error.store(true);
					return false;
				}
			}
			return true;
		}
	};

	Writer::Writer(IPipe* primary, const std::vector<IPipe*>& additional_pipes)
		: impl(new Impl(primary, additional_pipes))
	{
	}

	Writer::~Writer() = default;

	size_t Writer::Read(char* buffer, size_t bsize, int timeoutms)
	{
		return impl->lanes[0]->pipe->Read(buffer, bsize, timeoutms);
	}

	bool Writer::Write(const char* buffer, size_t bsize, int timeoutms, bool flush)
	{
		return impl->write(buffer, bsize, timeoutms, flush);
	}

	size_t Writer::Read(std::string* ret, int timeoutms)
	{
		return impl->lanes[0]->pipe->Read(ret, timeoutms);
	}

	bool Writer::Write(const std::string& str, int timeoutms, bool flush)
	{
		return impl->write(str.data(), str.size(), timeoutms, flush);
	}

	bool Writer::Flush(int timeoutms) { return impl->flush(timeoutms); }
	bool Writer::isWritable(int timeoutms) { return !impl->error.load() && impl->lanes[0]->pipe->isWritable(timeoutms); }
	bool Writer::isReadable(int timeoutms) { return impl->lanes[0]->pipe->isReadable(timeoutms); }
	bool Writer::hasError() { return impl->error.load(); }
	void Writer::shutdown()
	{
		impl->error.store(true);
		for (size_t i = 0; i < impl->lanes.size(); ++i) impl->lanes[i]->pipe->shutdown();
		impl->changed.notify_all();
	}
	size_t Writer::getNumElements() { return 0; }
	size_t Writer::getNumWaiters() { return 0; }
	void Writer::addThrottler(IPipeThrottler* throttler) { for (size_t i = 0; i < impl->lanes.size(); ++i) impl->lanes[i]->pipe->addThrottler(throttler); }
	void Writer::addOutgoingThrottler(IPipeThrottler* throttler) { for (size_t i = 0; i < impl->lanes.size(); ++i) impl->lanes[i]->pipe->addOutgoingThrottler(throttler); }
	void Writer::addIncomingThrottler(IPipeThrottler* throttler) { for (size_t i = 0; i < impl->lanes.size(); ++i) impl->lanes[i]->pipe->addIncomingThrottler(throttler); }
	_i64 Writer::getTransferedBytes()
	{
		_i64 ret = 0;
		for (size_t i = 0; i < impl->lanes.size(); ++i) ret += impl->lanes[i]->pipe->getTransferedBytes();
		return ret;
	}
	void Writer::resetTransferedBytes() { for (size_t i = 0; i < impl->lanes.size(); ++i) impl->lanes[i]->pipe->resetTransferedBytes(); }
	_i64 Writer::getRealTransferredBytes()
	{
		_i64 ret = 0;
		for (size_t i = 0; i < impl->lanes.size(); ++i) ret += impl->lanes[i]->pipe->getRealTransferredBytes();
		return ret;
	}

	void Writer::setUsageString(const std::string& str)
	{
		for (size_t i = 0; i < impl->lanes.size(); ++i) impl->lanes[i]->pipe->setUsageString(str);
	}

	bool Writer::setCompressionSettings(const SCompressionSettings& params)
	{
		bool ok = true;
		for (size_t i = 0; i < impl->lanes.size(); ++i)
		{
			if (!impl->lanes[i]->pipe->setCompressionSettings(params)) ok = false;
		}
		return ok;
	}

	bool Writer::setOption(const SocketOption opt)
	{
		bool ok = true;
		for (size_t i = 0; i < impl->lanes.size(); ++i)
		{
			if (!impl->lanes[i]->pipe->setOption(opt)) ok = false;
		}
		return ok;
	}
}
