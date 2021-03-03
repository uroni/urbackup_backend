#include "LocalFileBackup.h"
#include "../urbackupcommon/filelist_utils.h"
#include "../common/data.h"
#include "client.h"
#include <assert.h>

_i64 LocalFileBackup::getIncrementalSize(IFile* f, const std::vector<size_t>& diffs, bool& backup_with_components, bool all)
{
	f->Seek(0);
	_i64 rsize = 0;
	FileListParser list_parser;
	SFile cf;
	bool indirchange = false;
	size_t read;
	size_t line = 0;
	char buffer[4096];
	int indir_currdepth = 0;
	int depth = 0;
	int indir_curr_depth = 0;
	int changelevel = 0;
	backup_with_components = false;

	if (all)
	{
		indirchange = true;
	}

	while ((read = f->Read(buffer, 4096)) > 0)
	{
		for (size_t i = 0; i < read; ++i)
		{
			bool b = list_parser.nextEntry(buffer[i], cf, NULL);
			if (b)
			{
				if (cf.isdir == true)
				{
					if (depth == 0
						&& cf.name == "windows_components_config")
					{
						backup_with_components = true;
					}

					if (indirchange == false && hasChange(line, diffs))
					{
						indirchange = true;
						changelevel = depth;
						indir_currdepth = 0;
					}
					else if (indirchange == true)
					{
						if (cf.name != "..")
							++indir_currdepth;
						else
							--indir_currdepth;
					}

					if (cf.name == ".." && indir_currdepth > 0)
					{
						--indir_currdepth;
					}

					if (cf.name != "..")
					{
						++depth;
					}
					else
					{
						--depth;
						if (indirchange == true && depth == changelevel)
						{
							if (!all)
							{
								indirchange = false;
							}
						}
					}
				}
				else
				{
					if (indirchange == true || hasChange(line, diffs))
					{
						if (cf.size > 0)
						{
							rsize += cf.size;
						}
					}
				}
				++line;
			}
		}

		if (read < 4096)
			break;
	}

	return rsize;
}

bool LocalFileBackup::hasChange(size_t line, const std::vector<size_t>& diffs)
{
	return std::binary_search(diffs.begin(), diffs.end(), line);
}

void LocalFileBackup::referenceShadowcopy(const std::string& name, const std::string& server_token, const std::string& clientsubname)
{
	CWData data;
	data.addChar(IndexThread::IndexThreadAction_ReferenceShadowcopy);
	unsigned int curr_result_id = IndexThread::getResultId();
	data.addUInt(curr_result_id);
	data.addString(name);
	data.addString(server_token);
	data.addUChar(0);
	data.addUChar(1);
	data.addString(clientsubname);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
}

void LocalFileBackup::unreferenceShadowcopy(const std::string& name, const std::string& server_token, const std::string& clientsubname, int issues)
{
	CWData data;
	data.addChar(IndexThread::IndexThreadAction_ReleaseShadowcopy);
	unsigned int curr_result_id = IndexThread::getResultId();
	data.addUInt(curr_result_id);
	data.addString(name);
	data.addString(server_token);
	data.addUChar(0);
	data.addInt(-1);
	data.addString(clientsubname);
	data.addInt(issues);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
}

