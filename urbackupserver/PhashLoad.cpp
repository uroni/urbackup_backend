#include "PhashLoad.h"
#include "PhashLoad.h"
#include "PhashLoad.h"
#include "PhashLoad.h"
#include "PhashLoad.h"
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "ClientMain.h"
#include "server_settings.h"
#include "database.h"
#include "../common/data.h"

extern std::string server_token;

PhashLoad::PhashLoad(FileClient* fc,
	logid_t logid, std::string async_id)
	: has_error(false), fc(fc),
	logid(logid), async_id(async_id),
	phash_file_pos(0), phash_file(NULL), eof(false),
	has_timeout_error(false)
{
}

PhashLoad::~PhashLoad()
{
	ScopedDeleteFile del_file(phash_file);
}

void PhashLoad::operator()()
{
	fc->setProgressLogCallback(NULL);
	fc->setNoFreeSpaceCallback(NULL);

	std::string cfn = "SCRIPT|phash_{9c28ff72-5a74-487b-b5e1-8f1c96cd0cf4}/phash_" + async_id + "|0|" + convert(Server->getRandomNumber()) + "|" + server_token;
	phash_file = Server->openTemporaryFile();

	if (phash_file == NULL)
	{
		ServerLogger::Log(logid, "Error opening random file for parallel hash load", LL_ERROR);
		has_error = true;
		return;
	}

	fc->setReconnectTries(5000);
	_u32 rc = fc->GetFile(cfn, phash_file, true, false, 0, true, 0);

	if (rc != ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error during parallel hash load: "+fc->getErrorString(rc), LL_ERROR);
		has_error = true;

		if (rc == ERR_CONN_LOST || rc == ERR_TIMEOUT)
		{
			has_timeout_error = true;
		}

		return;
	}
	else
	{
		fc->FinishScript(cfn);
	}
}

bool PhashLoad::getHash(int64 file_id, std::string & hash)
{
	while (true)
	{
		while (phash_file == NULL
			|| phash_file_pos + static_cast<int64>(sizeof(_u16)) > phash_file->Size())
		{
			if ((has_error || eof)
				&& phash_file_pos + static_cast<int64>(sizeof(_u16)) > phash_file->Size())
			{
				return false;
			}
			Server->wait(1000);
		}

		_u16 msgsize;
		if (phash_file->Read(phash_file_pos, reinterpret_cast<char*>(&msgsize), sizeof(msgsize)) != sizeof(msgsize))
		{
			return false;
		}

		msgsize = little_endian(msgsize);

		while (phash_file_pos + static_cast<int64>(sizeof(_u16)) + msgsize > phash_file->Size())
		{
			if ((has_error || eof)
				&& phash_file_pos + static_cast<int64>(sizeof(_u16)) + msgsize > phash_file->Size())
			{
				return false;
			}
			Server->wait(1000);
		}

		std::string msgdata = phash_file->Read(phash_file_pos + sizeof(_u16), msgsize);
		if (msgdata.size() != msgsize)
		{
			return false;
		}

		phash_file_pos += sizeof(_u16) + msgsize;

		CRData data(msgdata.data(), msgdata.size());
		char id;
		if (!data.getChar(&id))
			return false;

		if (id == 0)
			continue;

		if (id != 1)
			return false;

		int64 curr_file_id;
		if (!data.getVarInt(&curr_file_id))
			return false;

		if (!data.getStr2(&hash))
			return false;

		if (curr_file_id > file_id)
		{
			phash_file_pos -= sizeof(_u16) + msgsize;
			hash.clear();
			return false;
		}
		else if (curr_file_id < file_id)
		{
			hash.clear();
			continue;
		}

		return true;
	}
}

bool PhashLoad::hasError()
{
	return has_error;
}

void PhashLoad::shutdown()
{
	fc->Shutdown();
}

bool PhashLoad::isDownloading()
{
	return fc->isDownloading();
}

void PhashLoad::setProgressLogEnabled(bool b)
{
	if (b)
	{
		fc->setProgressLogCallback(orig_progress_log_callback);
	}
	else
	{
		fc->setProgressLogCallback(NULL);
	}
}
