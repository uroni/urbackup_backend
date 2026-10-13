#pragma once
#include "../Interface/File.h"
#include "../Interface/Mutex.h"
#include "../urbackupcommon/image_read_protocol.h"
#include <deque>
#include <atomic>
#include <memory>

/**
* Provides channels for ImageReadFile to reconnect to the server
*/
class IImageReadChannel
{
public:
	virtual ~IImageReadChannel() {}

	//Starts random access reads of the image on a new channel. Returns NULL on failure.
	virtual IPipe* reopen(int64 size) = 0;

	//Releases a (broken) channel pipe
	virtual void release(IPipe* pipe) = 0;
};

/**
* Read-only file backed by random access reads of an image backup on the server
* (see image_read_protocol.h), sent via the channel pipe.
* Sequential reads are pipelined by queuing read-ahead requests. If a read does
* not match the next queued read-ahead request, the queued responses are drained
* before the actual read is requested.
* If channel is set and the connection to the server fails, it reconnects via
* a new channel and retries the reads instead of failing them.
*/
class ImageReadFile : public IFile
{
public:
	ImageReadFile(IPipe* pipe, int64 size, std::unique_ptr<IImageReadChannel> channel = nullptr);

	~ImageReadFile();

	virtual std::string Read(_u32 tr, bool *has_error = NULL);

	virtual std::string Read(int64 spos, _u32 tr, bool *has_error = NULL);

	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error = NULL);

	virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error = NULL);

	virtual _u32 Write(const std::string &tw, bool *has_error = NULL);

	virtual _u32 Write(int64 spos, const std::string &tw, bool *has_error = NULL);

	virtual _u32 Write(const char* buffer, _u32 bsiz, bool *has_error = NULL);

	virtual _u32 Write(int64 spos, const char* buffer, _u32 bsiz, bool *has_error = NULL);

	virtual bool Seek(_i64 spos);

	virtual _i64 Size(void);

	virtual _i64 RealSize();

	virtual bool PunchHole(_i64 spos, _i64 size);

	virtual bool Sync();

	virtual std::string getFilename(void);

	//Sends a keepalive if idle. Returns false if the connection is broken.
	bool keepalive();

	//Ends the random access session
	void close();

	//Stops reconnecting (reads in progress fail). Does not need the mutex.
	void cancelReconnect();

	//Current channel pipe (NULL if broken and not reconnected)
	IPipe* getPipe();

private:
	//Marks the connection as broken. Mutex must be locked.
	void setConnectionError();

	//Single reconnect attempt. Mutex must be locked.
	bool tryReconnect();

	//Reconnects, retrying until it succeeds or reconnect_timeout elapsed. Mutex must be locked.
	bool reconnect();

	//Sends request and receives the response header. Mutex must be locked.
	bool request(const SImageReadRequest& req, int* err);

	//Sends the requests without waiting for responses. Mutex must be locked.
	bool sendRequests(const SImageReadRequest* reqs, size_t n);

	//Receives the next response header. Mutex must be locked.
	bool receiveResponse(int* err);

	//Queues read-ahead requests of len bytes following the queued ones (or read_end if none are queued). Mutex must be locked.
	bool queueReadahead(int64 read_end, _u32 len);

	//Receives and discards responses of queued read-ahead requests. Mutex must be locked.
	bool drainReadahead();

	IPipe* pipe;
	int64 size;
	int64 pos;
	IMutex* mutex;
	bool connection_error;
	int64 connection_error_time;
	bool closed;
	std::atomic<bool> reconnect_cancelled;
	std::unique_ptr<IImageReadChannel> channel;
	int64 last_request;

	//Sent read-ahead requests whose responses were not received yet, in order
	std::deque<SImageReadRequest> readahead;
	//Response header of the first read-ahead request was received and its data partially consumed
	bool readahead_partial;
	int64 readahead_bytes;
	int64 readahead_window;
	int64 last_read_end;
	//Number of consecutive reads continuing the previous read
	int sequential_reads;
	//Heuristic: reads are sequential, so do read-ahead
	bool read_sequential;
	int64 last_read_time;
};
