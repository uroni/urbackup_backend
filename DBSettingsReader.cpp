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

#include "Interface/Types.h"
#include "Interface/Database.h"

#include "SettingsReader.h"
#include "stringtools.h"
#include "Server.h"

#include "DBSettingsReader.h"

#include <iostream>

CDBSettingsReader::CDBSettingsReader(THREAD_ID tid, DATABASE_ID did, const std::string &pTable, const std::string &pSQL)
{
	table=pTable;
	IDatabase *db=Server->getDatabase(tid, did);
	if(pSQL.empty() )
		query=db->Prepare("SELECT value FROM "+table+" WHERE key=?");
	else
		query=db->Prepare(pSQL);
}

CDBSettingsReader::CDBSettingsReader(IDatabase *pDB, const std::string &pTable, const std::string &pSQL)
{
	table=pTable;
	if(pSQL.empty() )
		query=pDB->Prepare("SELECT value FROM "+table+" WHERE key=?");
	else
		query=pDB->Prepare(pSQL);
}

bool CDBSettingsReader::getValue(std::string key, std::string *value)
{
	if(query==NULL)
	{
		return false;
	}

	query->Bind(key);
	db_nresults res=query->ReadN();
	query->Reset();

	if( res.size()>0 )
	{
		*value=res[0]["value"];
		return true;
	}
	else
		return false;
}

bool CDBSettingsReader::getValue(std::wstring key, std::wstring *value)
{
	if(query==NULL)
	{
		return false;
	}

	query->Bind(key);
	db_results res=query->Read();
	query->Reset();

	if( res.size()>0 )
	{
		*value=res[0][L"value"];
		return true;
	}
	else
		return false;
}


