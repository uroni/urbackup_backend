/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#define _exit exit
#include <unistd.h>
#endif


#define DEF_SERVER
#include "../Interface/Server.h"

#include "pluginmgr.h"

//---
#include "CryptoFactory.h"

IServer *Server;

CCryptoPluginMgr *cryptopluginmgr=NULL;

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	cryptopluginmgr=new CCryptoPluginMgr;

	Server->RegisterPluginThreadsafeModel( cryptopluginmgr, "cryptoplugin");

	std::string crypto_action=Server->getServerParameter("crypto_action");

	if(!crypto_action.empty())
	{
		if(crypto_action=="generate_keys")
		{
			std::string key_name=Server->getServerParameter("key_name");
			CryptoFactory fak;
			bool b=fak.generatePrivatePublicKeyPair(key_name);
			if(b)
			{
				Server->Log("Keys generated successfully", LL_INFO);
			}
			else
			{
				Server->Log("Generating keys failed", LL_ERROR);
			}
		}
		else if(crypto_action=="sign_file")
		{
			std::string sign_filename=Server->getServerParameter("sign_filename");
			std::string keyfile=Server->getServerParameter("keyfile");
			std::string signature_filename=Server->getServerParameter("signature_filename");
			CryptoFactory fak;
			bool b=fak.signFile(keyfile, sign_filename, signature_filename);
			if(!b)
			{
				Server->Log("Signing file failed", LL_INFO);
			}
			else
			{
				Server->Log("Signed file successfully", LL_INFO);
			}
		}
		else if(crypto_action=="verify_file")
		{
			std::string verify_filename=Server->getServerParameter("verify_filename");
			std::string keyfile=Server->getServerParameter("keyfile");
			std::string signature_filename=Server->getServerParameter("signature_filename");
			CryptoFactory fak;
			bool b=fak.verifyFile(keyfile, verify_filename, signature_filename);
			if(!b)
			{
				Server->Log("Verifying file failed", LL_INFO);
			}
			else
			{
				Server->Log("Verfifed file successfully", LL_INFO);
			}
		}
		else
		{
			Server->Log("Unknown crypto_action");
		}
		_exit(0);
	}

	Server->Log("Loaded -cryptoplugin- plugin", LL_INFO);
}

DLLEXPORT void UnloadActions(void)
{
}

