#ifndef CFILECLIENT_H
#define CFILECLIENT_H

#include <deque>
#include "packet_ids.h"
#include "../../urbackupcommon/fileclient/tcpstack.h"
#include "socket_header.h"
#include "../../Interface/Pipe.h"
#include "../../Interface/File.h"
#include "../../Interface/Mutex.h"

#define TCP_PORT 35621
#define UDP_PORT 35622
#define UDP_SOURCE_PORT 35623


#define BUFFERSIZE 32768
#define NBUFFERS   32
#define NUM_FILECLIENTS 5
#define VERSION 36
#define WINDOW_SIZE 512*1024
#define BUFFERSIZE_UDP 4096
//#define DISABLE_WINDOW_SIZE

typedef unsigned int _u32;
typedef int _i32;
typedef unsigned short _u16;
typedef __int64 _u64;

#define SERVER_TIMEOUT_BACKUPPED 120000
#define SERVER_TIMEOUT 120000
#define LOG

class CRData;

class FileClient
{
public:

		class ReconnectionCallback
		{
		public:
			virtual IPipe * new_fileclient_connection(void)=0;
		};

		class NoFreeSpaceCallback
		{
		public:
			virtual bool handle_not_enough_space(const std::string &path)=0;
		};

		enum MetadataQueue
		{
			MetadataQueue_Data,
			MetadataQueue_Metadata,
			MetadataQueue_MetadataAndHash
		};

		class QueueCallback
		{
		public:
			virtual std::string getQueuedFileFull(MetadataQueue& metadata, size_t& folder_items) = 0;
			virtual void unqueueFileFull(const std::string& fn) = 0;
			virtual void resetQueueFull() = 0;
		};

		class ProgressLogCallback
		{
		public:
			virtual void log_progress(const std::string& fn, int64 total, int64 downloaded, int64 speed_bps) = 0;
		};


		FileClient(bool enable_find_servers, std::string identity, int protocol_version=0, bool internet_connection=false,
			FileClient::ReconnectionCallback *reconnection_callback=NULL,
			FileClient::NoFreeSpaceCallback *nofreespace_callback=NULL);
        ~FileClient(void);

		_u32 GetServers(bool start, const std::vector<in_addr> &addr_hints);
        std::vector<sockaddr_in> getServers(void);
        std::vector<sockaddr_in> getWrongVersionServers(void);
        std::vector<std::string> getServerNames(void);
        _u32 getLocalIP(void);
        void setServerName(std::string pName);
        std::string getServerName(void);
        int getMaxVersion(void);
        bool isConnected(void);
        bool ListDownloaded(void);

        _u32 Connect(sockaddr_in *addr=NULL);
		_u32 Connect(IPipe *cp);
		
		void Shutdown();

        //---needs Connection
        _u32 GetFile(std::string remotefn, IFile *file, bool hashed, bool metadata_only, bool with_timeout=true, size_t folder_items=0);

		_u32 GetFileHashAndMetadata(std::string remotefn, std::string& hash, std::string& permissions, int64& filesize, int64& created, int64& modified);

		_u32 InformMetadataStreamEnd(const std::string& server_token);

		void addThrottler(IPipeThrottler *throttler);

		_i64 getTransferredBytes(void);

		_i64 getRealTransferredBytes();

		_i64 getReceivedDataBytes(void);
		void resetReceivedDataBytes(void);

		static std::string getErrorString(_u32 ec);

		void setReconnectionTimeout(unsigned int t);

		void setQueueCallback(FileClient::QueueCallback* cb);

		void setProgressLogCallback(FileClient::ProgressLogCallback* cb);

		_u32 Flush();

		bool Reconnect(void);
              
private:
		void bindToNewInterfaces();

		void fillQueue();

		void logProgress(const std::string& remotefn, _u64 filesize, _u64 received);

        std::vector<SOCKET> udpsocks;
		std::vector<sockaddr_in> broadcast_addrs;
		std::vector<_u32> broadcast_iface_addrs;
        IPipe *tcpsock;

        int64 starttime;
        int64 connect_starttime;

        bool socket_open;
        bool connected;

        char buffer[BUFFERSIZE_UDP];

        sockaddr_in serveraddr;

        std::vector<sockaddr_in> servers;
        std::vector<sockaddr_in> wvservers;
        std::vector<std::string> servernames;
        std::vector<std::string> games;

        CTCPStack stack;

        _u32 local_ip;

        std::string mServerName;

        int max_version;

	sockaddr_in server_addr;

	int connection_id;

		int protocol_version;
		bool internet_connection;

		_i64 real_transferred_bytes;
		_i64 transferred_bytes;
		std::vector<IPipeThrottler*> throttlers;

		FileClient::ReconnectionCallback *reconnection_callback;
		FileClient::NoFreeSpaceCallback *nofreespace_callback;

		FileClient::QueueCallback* queue_callback;
		unsigned int reconnection_timeout;

		bool retryBindToNewInterfaces;
		
		std::string identity;

		_i64 received_data_bytes;

		IMutex* mutex;

		std::deque<std::string> queued;

		char dl_buf[BUFFERSIZE];
		size_t dl_off;

		int64 last_transferred_bytes;
		int64 last_progress_log;
		FileClient::ProgressLogCallback* progress_log_callback;

		bool needs_flush;
};

const _u32 ERR_CONTINUE=0;
const _u32 ERR_SUCCESS=1;
const _u32 ERR_TIMEOUT=2;
const _u32 ERR_FILE_DOESNT_EXIST=3;
const _u32 ERR_SOCKET_ERROR=4;
const _u32 ERR_CONNECTED=5;
const _u32 ERR_ERROR=6;
const _u32 ERR_BASE_DIR_LOST=7;
const _u32 ERR_HASH=8;
const _u32 ERR_INT_ERROR=9;
const _u32 ERR_CONN_LOST=10;
const _u32 ERR_ERRORCODES=11;

const _u32 sleeptime=50;

#endif
