/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#include "../fileservplugin/IFileServ.h"
#include "tokens.h"
#include "clientdao.h"
#include "database.h"
#include "client.h"

namespace
{
	class TokenCallback : public IFileServ::ITokenCallback
	{
	public:
		TokenCallback()
			: client_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT))
		{
		}

		virtual std::string getFileTokens( const std::string& fn )
		{
			return tokens::get_file_tokens(fn, &client_dao, tokens::ETokenRight_Read, token_cache);
		}

		virtual std::string translateTokens(int64 uid, int64 gid, int64 mode)
		{
			return std::string();
		}

	private:
		tokens::TokenCache token_cache;
		ClientDAO client_dao;	
	};

	class TokenCallbackFactory : public IFileServ::ITokenCallbackFactory
	{
	public:

		virtual IFileServ::ITokenCallback* getTokenCallback()
		{
			return new TokenCallback;
		}

	};
}

void register_token_callback()
{
	static TokenCallbackFactory* token_callback_factory = new TokenCallbackFactory();
	IFileServ* filesrv = IndexThread::getFileSrv();
	if(filesrv!=NULL)
	{
		filesrv->registerTokenCallbackFactory(token_callback_factory);
	}
}