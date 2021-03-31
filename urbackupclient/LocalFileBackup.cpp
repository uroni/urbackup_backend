#include "LocalFileBackup.h"
#include "../urbackupcommon/filelist_utils.h"
#include "../common/data.h"
#include "client.h"
#include <assert.h>

LocalFileBackup::LocalFileBackup(IBackupFileSystem* backup_files, int64 local_process_id, int64 server_log_id, int64 server_status_id, int64 backupid, std::string server_token, std::string server_identity, int facet_id)
	: LocalBackup(backup_files, local_process_id, server_log_id, server_status_id, backupid, std::move(server_token), std::move(server_identity), facet_id)
{
	file_metadata_pipe.reset(IndexThread::getFileSrv()->getFileMetadataPipe());
}

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

					if (!indirchange && hasChange(line, diffs))
					{
						indirchange = true;
						changelevel = depth;
						indir_currdepth = 0;
					}
					else if (indirchange)
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
						if (indirchange && depth == changelevel)
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
					if (indirchange || hasChange(line, diffs))
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

std::string LocalFileBackup::permissionsAllowAll()
{
	CWData token_info;
	//allow to all
	token_info.addChar(0);
	token_info.addVarInt(0);

	return std::string(token_info.getDataPtr(), token_info.getDataSize());
}

namespace
{
	const int64 win32_meta_magic = little_endian(0x320FAB3D119DCB4AULL);
	const int64 unix_meta_magic = little_endian(0xFE4378A3467647F0ULL);
}

bool LocalFileBackup::backupMetadata(IFsFile* metadataf, const std::string& sourcepath, FileMetadata* metadata)
{
	if (metadata != nullptr)
	{
		SFile file_meta = getFileMetadata(os_file_prefix(sourcepath));
		if (file_meta.name.empty())
		{
			Server->Log("Error getting metadata (created and last modified time) of " + sourcepath, LL_ERROR);
			return false;
		}
		metadata->accessed = file_meta.accessed;
		metadata->created = file_meta.created;
		metadata->last_modified = file_meta.last_modified;

		int64 truncate_to_bytes;
		if (!write_file_metadata(metadataf, NULL, *metadata, true, truncate_to_bytes))
		{
			//Log
			return false;
		}
		else
		{
			if (!metadataf->Resize(truncate_to_bytes))
			{
				//Log
				return false;
			}
		}
	}

	int64 osm_offset = os_metadata_offset(metadataf);

	if (osm_offset == -1)
	{
		Server->Log("Error saving metadata. Metadata offset cannot be calculated at \"" + metadataf->getFilename() + "\"", LL_ERROR);
		return false;
	}

	if (!metadataf->Seek(osm_offset))
	{
		Server->Log("Error saving metadata. Could not seek to end of file \"" + metadataf->getFilename() + "\"", LL_ERROR);
		return false;
	}

	if (!writeOsMetadata(sourcepath, osm_offset, metadataf))
	{
		//Log
		return false;
	}

	return true;
}

bool LocalFileBackup::writeOsMetadata(const std::string& sourcefn, int64 dest_start_offset, IFile* dest)
{
	int64 win32_magic_and_size[2];
	win32_magic_and_size[1] = win32_meta_magic;
	win32_magic_and_size[0] = 0;

	if (dest->Write(reinterpret_cast<char*>(win32_magic_and_size), sizeof(win32_magic_and_size)) != sizeof(win32_magic_and_size))
	{
		//Log
		return false;
	}

	if (!file_metadata_pipe->openOsMetadataFile(sourcefn))
	{
		//Log
		return false;
	}

	std::vector<char> buf(256 * 1024);

	size_t read;
	while (file_metadata_pipe->readCurrOsMetadata(buf.data(), buf.size(), read))
	{
		if (dest->Write(buf.data(), read) != read)
		{
			//Log
			return false;
		}

		win32_magic_and_size[0] += read;
	}

	if (!dest->Seek(dest_start_offset))
	{
		//Log
		return false;
	}

	if (dest->Write(reinterpret_cast<char*>(win32_magic_and_size), sizeof(win32_magic_and_size[0])) != sizeof(win32_magic_and_size[0]))
	{
		//Log
		return false;
	}

	return true;
}