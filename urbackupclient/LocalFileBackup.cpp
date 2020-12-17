#include "LocalFileBackup.h"
#include "../urbackupcommon/filelist_utils.h"
#include "../common/data.h"
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