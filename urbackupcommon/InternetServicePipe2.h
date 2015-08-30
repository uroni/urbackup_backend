#pragma once

#include "../Interface/Pipe.h"
#include <memory>

class IAESGCMEncryption;
class IAESGCMDecryption;

#ifndef HAS_IINTERNET_SERVICE_PIPE
#define HAS_IINTERNET_SERVICE_PIPE
class IInternetServicePipe : public IPipe
{
public:
	virtual std::string decrypt(const std::string &data) = 0;
	virtual std::string encrypt(const std::string &data) = 0;

	virtual void destroyBackendPipeOnDelete(bool b)=0;
	virtual void setBackendPipe(IPipe *pCS)=0;

	virtual IPipe *getRealPipe(void)=0;
};
#endif

class InternetServicePipe2 : public IInternetServicePipe
{
public:
	InternetServicePipe2();
	InternetServicePipe2(IPipe *cs, const std::string &key);
	~InternetServicePipe2();

	void init(IPipe *pcs, const std::string &key);

	virtual size_t Read( char *buffer, size_t bsize, int timeoutms=-1 );

	virtual size_t Read( std::string *ret, int timeoutms=-1 );

	virtual bool Write( const char *buffer, size_t bsize, int timeoutms=-1, bool flush=true );

	virtual bool Write( const std::string &str, int timeoutms=-1, bool flush=true );

	virtual bool Flush(int timeoutms=-1);

	virtual bool isWritable( int timeoutms=0 );

	virtual bool isReadable( int timeoutms=0 );

	virtual bool hasError( void );

	virtual void shutdown( void );

	virtual size_t getNumElements( void );

	virtual void addThrottler( IPipeThrottler *throttler );

	virtual void addOutgoingThrottler( IPipeThrottler *throttler );

	virtual void addIncomingThrottler( IPipeThrottler *throttler );

	virtual _i64 getTransferedBytes( void );

	virtual void resetTransferedBytes( void );

	virtual std::string decrypt( const std::string &data );

	virtual std::string encrypt( const std::string &data );

	virtual void destroyBackendPipeOnDelete(bool b);
	virtual void setBackendPipe(IPipe *pCS);

	virtual IPipe *getRealPipe();

	int64 getEncryptionOverheadBytes();

private:
	std::auto_ptr<IAESGCMDecryption> dec;
	std::auto_ptr<IAESGCMEncryption> enc;

	IPipe *cs;
	bool destroy_cs;
	bool has_error;
};