/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
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

#include "vld.h"
#include "Query.h"
#include "Server.h"
#include "sqlite/sqlite3.h"
#include "stringtools.h"
#include "utf8/utf8.h"
#include <memory.h>

CQuery::CQuery(const std::string &pStmt_str, sqlite3_stmt *prepared_statement, CDatabase *pDB) : stmt_str(pStmt_str)
{
	ps=prepared_statement;
	curr_idx=1;
	db=pDB;
}

CQuery::~CQuery()
{
	int err=sqlite3_finalize(ps);
	if( err!=SQLITE_OK )
		Server->Log("SQL: "+(std::string)sqlite3_errmsg(db->getDatabase())+ " Stmt: ["+stmt_str+"]", LL_ERROR);
}

void CQuery::Bind(const std::string &str)
{
	int err=sqlite3_bind_text(ps, curr_idx, str.c_str(), (int)str.size(), SQLITE_TRANSIENT);
	if( err!=SQLITE_OK )
		Server->Log("Error binding text to Query  Stmt: ["+stmt_str+"]", LL_ERROR);
	++curr_idx;
}

void CQuery::Bind(const std::wstring &str)
{
	int err;
	if( sizeof(wchar_t)==2 )
	{
		err=sqlite3_bind_text16(ps, curr_idx, str.c_str(), (int)str.size()*2, SQLITE_TRANSIENT);
	}
	else
	{
		unsigned short *tmp=new unsigned short[str.size()];
		for(size_t i=0,l=str.size();i<l;++i)
		{
			tmp[i]=str[i];
		}
		err=sqlite3_bind_text16(ps, curr_idx, tmp, (int)str.size()*2, SQLITE_TRANSIENT);
		delete []tmp;
	}
	if( err!=SQLITE_OK )
		Server->Log("Error binding text to Query  Stmt: ["+stmt_str+"]", LL_ERROR);
	++curr_idx;
}

void CQuery::Bind(const char* buffer, _u32 bsize)
{
	int err=sqlite3_bind_blob(ps, curr_idx, buffer, bsize, SQLITE_TRANSIENT);
	if( err!=SQLITE_OK )
		Server->Log("Error binding blob to Query  Stmt: ["+stmt_str+"]", LL_ERROR);
	++curr_idx;
}

void CQuery::Bind(int p)
{
	int err=sqlite3_bind_int(ps, curr_idx, p);
	if( err!=SQLITE_OK )
		Server->Log("Error binding int to Query  Stmt: ["+stmt_str+"]", LL_ERROR);
	++curr_idx;
}

void CQuery::Bind(unsigned int p)
{
	Bind((int64)p);
}

void CQuery::Bind(double p)
{
	int err=sqlite3_bind_double(ps, curr_idx, p);
	if( err!=SQLITE_OK )
		Server->Log("Error binding double to Query  Stmt: ["+stmt_str+"]", LL_ERROR);
	++curr_idx;
}

void CQuery::Bind(int64 p)
{
	int err=sqlite3_bind_int64(ps, curr_idx, p);
	if( err!=SQLITE_OK )
		Server->Log("Error binding int64 to Query  Stmt: ["+stmt_str+"]", LL_ERROR);
	++curr_idx;
}

#ifdef _WIN64
void CQuery::Bind(size_t p)
{
	Bind((int64)p);
}
#endif

void CQuery::Reset(void)
{
	sqlite3_reset(ps);
	//sqlite3_clear_bindings(ps);
	curr_idx=1;
}

bool CQuery::Write(void)
{
	return Execute();
}

bool CQuery::Execute(void)
{
	bool transaction_lock=false;
	int tries=60; //10min
	int err=sqlite3_step(ps);
	while( err==SQLITE_BUSY || err==SQLITE_ROW || err==SQLITE_LOCKED )
	{
		if(err==SQLITE_BUSY)
		{
			if(transaction_lock==false)
			{
				if(db->LockForTransaction())
				{
					transaction_lock=true;
				}
				sqlite3_busy_timeout(db->getDatabase(), 10000);
			}
			else
			{
				--tries;
				if(tries<0)
				{
				  Server->Log("SQLITE: Giving up waiting for query  Stmt: ["+stmt_str+"]");
				  break;
				}
				Server->Log("SQLITE_BUSY in CQuery::Execute  Stmt: ["+stmt_str+"]", LL_ERROR);
			}
		}
		else if(err==SQLITE_LOCKED)
		{
			if(db->LockForTransaction())
			{
				transaction_lock=true;
			}				
			if(!db->WaitForUnlock())
			{
				Server->Log("DEADLOCK in CQuery::Execute  Stmt: ["+stmt_str+"]", LL_ERROR);
				Server->wait(1000);
			}
		}
		err=sqlite3_step(ps);
	}

	if(transaction_lock)
	{
		sqlite3_busy_timeout(db->getDatabase(), 50);
		db->UnlockForTransaction();
	}

	if( err!=SQLITE_DONE )
	{
		Server->Log("Error in CQuery::Execute - "+(std::string)sqlite3_errmsg(db->getDatabase()) +"  Stmt: ["+stmt_str+"]", LL_ERROR);
		return false;
	}

	return true;
}

db_nresults CQuery::ReadN(void)
{
	int err;
	db_nresults rows;

	while( (err=sqlite3_step(ps))==SQLITE_BUSY || err==SQLITE_ROW)
	{
		if( err==SQLITE_ROW )
		{
			db_nsingle_result res;
			int column=0;
			const char *column_name;
			while( (column_name=sqlite3_column_name(ps, column) )!=NULL )
			{
				std::string data;
				const void *blob=sqlite3_column_blob(ps, column);
				int blob_size=sqlite3_column_bytes(ps, column);
				data.resize(blob_size);
				memcpy(&data[0], blob, blob_size);
				res.insert( std::pair<std::string, std::string>(column_name, data) );
				++column;
			}
			rows.push_back( res );
		}
	}

	return rows;
}

db_results CQuery::Read(void)
{
	int err;
	db_results rows;

	while( (err=sqlite3_step(ps))==SQLITE_BUSY || err==SQLITE_ROW)
	{
		if( err==SQLITE_ROW )
		{
			db_single_result res;
			int column=0;
			const unsigned short *c_name;
			while( (c_name=(const unsigned short*)sqlite3_column_name16(ps, column) )!=NULL )
			{
				std::wstring column_name;
				if( sizeof(wchar_t)!=2 )
				{
					size_t len=0;
					while(c_name[len]!=0)
						++len;

					column_name.resize(len);
					for(size_t i=0;i<len;++i)
					{
						column_name[i]=c_name[i];
					}
				}
				else
				{
					column_name=(wchar_t*)c_name;
				}
				std::wstring data;
				int blob_size=sqlite3_column_bytes16(ps, column);
				const void *blob=sqlite3_column_blob(ps, column);
				//int blob_size2=sqlite3_column_bytes(ps, column);
				if(blob_size>0)
				{
					if( sizeof(wchar_t)==2 )
					{
						data.resize(blob_size/2+blob_size%2);
						memcpy(&data[0], blob, blob_size);
					}
					else
					{
						if( SQLITE_BLOB==sqlite3_column_type(ps, column) )
						{
							size_t size=(size_t)(blob_size/sizeof(wchar_t))+((blob_size%sizeof(wchar_t))>0?1:0);
							
							data.resize(size);
							char* ptr=(char*)data.c_str();
							memcpy(ptr, blob, blob_size);

							if( blob_size%sizeof(wchar_t)>0 )
							{
								memset(ptr+blob_size, 0, sizeof(wchar_t)-blob_size%sizeof(wchar_t) );
							}
						}
						else
						{
							data.resize(blob_size/sizeof(unsigned short));
							unsigned short *ip=(unsigned short*)blob;
							for(int i=0,l=blob_size/sizeof(unsigned short);i<l;++i)
							{
						
								data[i]=*ip;
								++ip;
							}
						}
					}
				}
				res.insert( std::pair<std::wstring, std::wstring>(column_name, data) );
				++column;
			}
			rows.push_back( res );
		}
		else
		{
			Server->wait(1000);
		}
	}

	return rows;
}
