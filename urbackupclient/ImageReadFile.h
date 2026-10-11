#pragma once
#include "../Interface/File.h"
#include "../Interface/Mutex.h"
#include "../urbackupcommon/image_read_protocol.h"

/**
* Read-only file backed by random access reads of an image backup on the server
* (see image_read_protocol.h), sent via the channel pipe.
*/
class ImageReadFile : public IFile
{
public:
	ImageReadFile(IPipe* pipe, int64 size);

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

private:
	//Sends request and receives the response header. Mutex must be locked.
	bool request(const SImageReadRequest& req, int* err);

	IPipe* pipe;
	int64 size;
	int64 pos;
	IMutex* mutex;
	bool connection_error;
	int64 last_request;
};
