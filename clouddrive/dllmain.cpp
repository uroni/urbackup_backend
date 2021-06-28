/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif

#ifndef STATIC_PLUGIN
#define DEF_SERVER
#endif

#include "../Interface/Server.h"
#include "pluginmgr.h"
#include "../stringtools.h"
#include "../urbackupcommon/WalCheckpointThread.h"

#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/logging/FormattedLogSystem.h>
#include <aws/core/Aws.h>

#include "../urbackupcommon/os_functions.h"

#ifndef STATIC_PLUGIN
IServer* Server;
#else
#include "../StaticPluginRegistration.h"

extern IServer* Server;

#define LoadActions LoadActions_clouddrive
#define UnloadActions UnloadActions_clouddrive
#endif

#include "KvStoreBackendS3.h"

void init_compress_encrypt_factory();

bool is_automount_finished()
{
    return false;
}

std::string getCdInterfacePath()
{
    os_create_dir("urbackup/cd");
    return "urbackup/cd";
}

class ServerLogging : public Aws::Utils::Logging::FormattedLogSystem
{
public:
    ServerLogging(Aws::Utils::Logging::LogLevel loglevel)
        : Aws::Utils::Logging::FormattedLogSystem(loglevel)
    {}

    void Flush() {}
protected:
    virtual void ProcessFormattedStatement(Aws::String&& statement)
    {
        Server->Log(trim(statement.c_str()), LL_INFO);
    }
};

ClouddrivePluginMgr* clouddrivepluginmgr;

DLLEXPORT void LoadActions(IServer* pServer)
{
    Server = pServer;

    WalCheckpointThread::init_mutex();
    KvStoreBackendS3::init_mutex();

    clouddrivepluginmgr = new ClouddrivePluginMgr;

    init_compress_encrypt_factory();

    Aws::SDKOptions options = {};
    Aws::InitAPI(options);

    Aws::Utils::Logging::InitializeAWSLogging(Aws::MakeShared<ServerLogging>("KvStoreBackend", Aws::Utils::Logging::LogLevel::Warn));

    Server->RegisterPluginThreadsafeModel(clouddrivepluginmgr, "clouddriveplugin");

#ifndef STATIC_PLUGIN
    Server->Log("Loaded -btrfsplugin- plugin", LL_INFO);
#endif
}

DLLEXPORT void UnloadActions(void)
{
    if (Server->getServerParameter("leak_check") == "true")
    {
        Aws::SDKOptions options;
        Aws::ShutdownAPI(options);
    }
}

#ifdef STATIC_PLUGIN
namespace
{
    static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 10);
}
#endif