/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "DatabaseCursor.h"
#include "Query.h"
#include "sqlite/sqlite3.h"
#include "Server.h"

DatabaseCursor::DatabaseCursor(CQuery *query, int *timeoutms)
	: query(query), transaction_lock(false), tries(60), timeoutms(timeoutms), lastErr(SQLITE_OK), _has_error(false)
{
	query->setupStepping(timeoutms);

#ifdef LOG_READ_QUERIES
	active_query=new ScopedAddActiveQuery(query);
#endif
}

DatabaseCursor::~DatabaseCursor(void)
{
	query->shutdownStepping(lastErr, timeoutms, transaction_lock);

#ifdef LOG_READ_QUERIES
	delete active_query;
#endif
}

bool DatabaseCursor::next(db_single_result &res)
{
	res.clear();
	do
	{
		bool reset=false;
		lastErr=query->step(res, timeoutms, tries, transaction_lock, reset);
		//TODO handle reset (should not happen in WAL mode)
		if(lastErr==SQLITE_ROW)
		{
			return true;
		}
	}
	while(query->resultOkay(lastErr));

	if(lastErr!=SQLITE_DONE)
	{
		Server->Log("SQL Error: "+query->getErrMsg()+ " Stmt: ["+query->getStatement()+"]", LL_ERROR);
		_has_error=true;
	}

	return false;
}

bool DatabaseCursor::has_error(void)
{
	return _has_error;
}