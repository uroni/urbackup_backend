#include "PersistentOpenFiles.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#include "file_permissions.h"

const char* persistent_open_files_fn = "urbackup\\open_files.dat";
const char* persistent_open_files_new_fn = "urbackup\\open_files.dat.new";

bool PersistentOpenFiles::cycle()
{
	Server->destroy(persistf);

	persistf = Server->openFile(persistent_open_files_new_fn, MODE_WRITE);

#ifndef _DEBUG
	change_file_permissions_admin_only(persistent_open_files_new_fn);
#endif

	if(!persistf) return false;

	bytes_deleted = 0;
	bytes_written = 0;

	for(std::map<std::wstring, unsigned int>::iterator it=open_files.begin();it!=open_files.end();++it)
	{
		addf(it->first, it->second);
	}

	flushf_int(false);

	Server->destroy(persistf);

	if(!os_rename_file(widen(persistent_open_files_new_fn), widen(persistent_open_files_fn)))
	{
		return false;
	}

	persistf = Server->openFile(widen(persistent_open_files_fn), MODE_RW);

	if(!persistf) return false;

	return true;
}

bool PersistentOpenFiles::flushf_int(bool allow_cycle)
{
	if(!persistf) return false;

	if(wdata.getDataSize()==0) return true;

	if(bytes_deleted>bytes_written && allow_cycle)
	{
		wdata.clear();
		return cycle();
	}

	_u32 w = persistf->Write(wdata.getDataPtr(), wdata.getDataSize());

	if(w!=wdata.getDataSize())
	{
		Server->Log("Error persisting open files to disk", LL_ERROR);

		wdata.clear();
		return false;
	}
	wdata.clear();

	if(persistf->Size()>1024 && allow_cycle)
	{
		return cycle();
	}

	return true;
}

bool PersistentOpenFiles::flushf()
{
	return flushf_int(true);
}

void PersistentOpenFiles::removef( unsigned int id, size_t fn_size )
{
	wdata.addChar(PERSIST_REMOVE);
	wdata.addInt64(Server->getTimeMS());
	wdata.addUInt(id);

	bytes_deleted+=1+sizeof(int64)+sizeof(id);
	bytes_deleted+=1+sizeof(int64)+sizeof(_u32)+fn_size+sizeof(id);
}

void PersistentOpenFiles::addf( const std::wstring& fn, unsigned int id )
{
	if(!persistf) return;

	wdata.addChar(PERSIST_ADD);
	wdata.addInt64(Server->getTimeMS());
	wdata.addString(Server->ConvertToUTF8(fn));
	wdata.addUInt(id);

	bytes_written+=1+sizeof(int64)+sizeof(_u32)+fn.size()+sizeof(id);
}

bool PersistentOpenFiles::load()
{
	if(!persistf)
	{
		Server->Log("No persisted open files list", LL_INFO);
		return false;
	}

	bytes_written=0;
	bytes_deleted=0;

	std::vector<char> data;
	data.resize(persistf->Size());

	size_t read=0;
	while(read<data.size())
	{
		read+=persistf->Read(data.data()+read, static_cast<_u32>(data.size()-read));
	}

	CRData pdata(data.data(), data.size());

	std::map<std::wstring, unsigned int> new_open_files;

	int64 max_time=0;

	int64 ctime = Server->getTimeMS();

	char id;
	while(pdata.getChar(&id))
	{
		if(id==PERSIST_ADD)
		{				
			int64 rec_time;
			if(!pdata.getInt64(&rec_time))
			{
				Server->Log("Error reading rec_time from persisted open files", LL_ERROR);
				return false;
			}
			std::string fn;
			if(!pdata.getStr(&fn))
			{
				Server->Log("Error reading fn from persisted open files", LL_ERROR);
				return false;
			}
			unsigned int id;
			if(!pdata.getUInt(&id))
			{
				Server->Log("Error reading id from persisted open files", LL_ERROR);
				return false;
			}

			new_open_files[Server->ConvertToUnicode(fn)]=id;

			curr_id=(std::max)(curr_id, id);

			max_time=(std::max)(max_time, rec_time);

			if(max_time>ctime)
			{
				Server->Log("Persisted open files list is obsolete (restart occured) (1)", LL_INFO);
				return false;
			}

			bytes_written+=1+sizeof(int64)+sizeof(_u32)+fn.size()+sizeof(id);
		}
		else if(id==PERSIST_REMOVE)
		{
			int64 rec_time;
			if(!pdata.getInt64(&rec_time))
			{
				Server->Log("Error reading rec_time from persisted open files (remove)", LL_ERROR);
				return false;
			}
			unsigned int id;
			if(!pdata.getUInt(&id))
			{
				Server->Log("Error reading id from persisted open files (remove)", LL_ERROR);
				return false;
			}

			bool found=false;
			for(std::map<std::wstring, unsigned int>::iterator it=new_open_files.begin();it!=new_open_files.end();++it)
			{
				if(it->second==id)
				{
					bytes_deleted+=1+sizeof(int64)+sizeof(id);
					bytes_deleted+=1+sizeof(int64)+sizeof(_u32)+it->first.size()+sizeof(id);

					found=true;
					new_open_files.erase(it);
					break;
				}
			}

			if(!found) return false;

			max_time=(std::max)(max_time, rec_time);

			if(max_time>ctime)
			{
				Server->Log("Persisted open files list is obsolete (restart occured) (2)", LL_INFO);
				return false;
			}
		}
		else
		{
			Server->Log("Unknown id while reading persisted open files", LL_ERROR);
			return false;
		}
	}

	if(max_time>Server->getTimeMS())
	{
		Server->Log("Persisted open files list is obsolete (restart occured)", LL_INFO);
		return false;
	}

	open_files = new_open_files;

	return true;
}

std::vector<std::wstring> PersistentOpenFiles::get()
{
	std::vector<std::wstring> ret;
	for(std::map<std::wstring, unsigned int>::iterator it=open_files.begin();it!=open_files.end();++it)
	{
		ret.push_back(it->first);
	}
	return ret;
}

void PersistentOpenFiles::remove( const std::wstring& fn )
{
	std::map<std::wstring, unsigned int>::iterator it=open_files.find(fn);
	if(it!=open_files.end())
	{
		removef(it->second, it->first.size());

		open_files.erase(it);
	}
}

void PersistentOpenFiles::add( const std::wstring& fn )
{
	if(open_files.find(fn)==open_files.end())
	{
		++curr_id;
		open_files[fn]=curr_id;

		addf(fn, curr_id);
	}
}

PersistentOpenFiles::PersistentOpenFiles() : curr_id(0), bytes_written(0), bytes_deleted(0)
{
	persistf = Server->openFile(persistent_open_files_fn, MODE_RW_CREATE);

#ifndef _DEBUG
	change_file_permissions_admin_only(persistent_open_files_fn);
#endif

	if(!load())
	{
		Server->destroy(persistf);

		persistf = Server->openFile(persistent_open_files_fn, MODE_WRITE);

#ifndef _DEBUG
		change_file_permissions_admin_only(persistent_open_files_fn);
#endif
	}
}

