#include "server_hash_existing.h"
#include "server_prepare_hash.h"
#include "../Interface/Server.h"
#include "server_log.h"

ServerHashExisting::ServerHashExisting( int clientid, BackupServerGet* server_get )
	: has_error(false), clientid(clientid), server_get(server_get)
{
	mutex = Server->createMutex();
	cond = Server->createCondition();
}


ServerHashExisting::~ServerHashExisting()
{
	Server->destroy(mutex);
	Server->destroy(cond);
}


void ServerHashExisting::operator()()
{
	while(true)
	{
		SHashItem item;
		{
			IScopedLock lock(mutex);
			while(queue.empty())
			{
				cond->wait(&lock);
			}

			item = queue.front();
			queue.pop_front();
		}

		if(item.do_stop)
		{
			return;
		}

		IFile* f = Server->openFile(item.fullpath, MODE_READ);

		if(f==NULL)
		{
			ServerLogger::Log(clientid, L"Error opening file \""+item.hashpath+L"\" for hashing", LL_WARNING);
			has_error = true;
		}
		else
		{
			ObjectScope destroy_f(f);

			int64 filesize = f->Size();
			std::string sha2 = BackupServerPrepareHash::hash_sha512(f);

			server_get->addExistingHash(item.fullpath, item.hashpath, sha2, filesize);
		}
	}
}

void ServerHashExisting::queueStop( bool front )
{
	SHashItem item;
	item.do_stop = true;
	IScopedLock lock(mutex);
	if(front)
	{
		queue.push_front(item);
	}
	else
	{
		queue.push_back(item);
	}
	cond->notify_one();
}

void ServerHashExisting::queueFile( const std::wstring& fullpath, const std::wstring& hashpath )
{
	SHashItem item;
	item.fullpath = fullpath;
	item.hashpath = hashpath;

	IScopedLock lock(mutex);
	queue.push_back(item);
	cond->notify_one();
}


