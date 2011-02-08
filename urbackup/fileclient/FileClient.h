#ifndef CFILECLIENT_H
#define CFILECLIENT_H

#include "packet_ids.h"
#include "tcpstack.h"
#include "socket_header.h"
#include "../../Interface/Pipe.h"
#include "../../Interface/File.h"

#define TCP_PORT 35621
#define UDP_PORT 35622


#define BUFFERSIZE 4096
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

class FileClient
{
public:
        FileClient(void);
        ~FileClient(void);

        _u32 GetServers(bool start);
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

        //---needs Connection
        _u32 GetGameList(void);
        std::vector<std::string> getGameList(void);

        _u32 GetFile(std::string remotefn, IFile *file);
              
private:
		bool Reconnect(void);

        SOCKET udpsock;
        IPipe *tcpsock;

        _u32 starttime;
        _u32 connect_starttime;

        bool socket_open;
        bool connected;

        _u32 num_games;
        _u32 num_games_get;
        bool num_games_res;
        bool res_name;

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
};

const _u32 ERR_CONTINUE=0;
const _u32 ERR_SUCCESS=1;
const _u32 ERR_TIMEOUT=2;
const _u32 ERR_FILE_DOESNT_EXIST=3;
const _u32 ERR_SOCKET_ERROR=4;
const _u32 ERR_CONNECTED=5;
const _u32 ERR_ERROR=6;
const _u32 ERR_BASE_DIR_LOST=7;

const _u32 sleeptime=50;

#endif
