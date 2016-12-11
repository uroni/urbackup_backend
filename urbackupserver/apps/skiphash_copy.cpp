#include "skiphash_copy.h"
#include <string>
#include "../../urbackupcommon/chunk_hasher.h"
#include "../../stringtools.h"
#include "../../urbackupcommon/os_functions.h"
#include <memory>

bool skiphash_copy(const std::string& src_path,
	const std::string& dst_path,
	const std::string& hashinput_path)
{
	std::auto_ptr<IFile> src(Server->openFile(os_file_prefix(src_path), MODE_READ_SEQUENTIAL));

	if(!src.get())
	{
		Server->Log("Could not open source file for reading at \"" + src_path+"\"", LL_ERROR);
		return false;
	}

	std::auto_ptr<IFile> hashoutput(Server->openFile(os_file_prefix(dst_path+".hash"), MODE_WRITE));

	if(!hashoutput.get())
	{
		Server->Log("Could not open hash output for writing at \""+dst_path+".hash\"", LL_ERROR);
		return false;
	}

	bool dst_exists = Server->fileExists(dst_path);

	std::auto_ptr<IFsFile> dst(Server->openFile(os_file_prefix(dst_path), MODE_RW));

	if(!dst.get())
	{
		dst.reset(Server->openFile(os_file_prefix(dst_path), MODE_WRITE));
	}

	if(!dst.get())
	{
		Server->Log("Could not open output file for writing at \""+dst_path+"\"", LL_ERROR);
		return false;
	}

	std::auto_ptr<IFile> hashinput;
	
	if(!hashinput_path.empty())
	{
		hashinput.reset(Server->openFile(os_file_prefix(hashinput_path), MODE_READ_DEVICE));

		if(!hashinput.get())
		{
			Server->Log("Could not open hash input file at \""+hashinput_path+"\"", LL_ERROR);
			return false;
		}
	}

	if(!dst_exists || !hashinput.get())
	{
		Server->Log("Destination file does not exist or there is no hash input. Performing full copy (with hash output)...", LL_INFO);
		return build_chunk_hashs(src.get(), hashoutput.get(), NULL, dst.get(), false, NULL, NULL, true);
	}
	else
	{
		int64 inplace_written = 0;
		bool ret = build_chunk_hashs(src.get(), hashoutput.get(), NULL,
			dst.get(), true, &inplace_written, hashinput.get(), true);
		float skipped_pc=0;
		if(src->Size()>0)
		{
			skipped_pc = 100.f - 100.f*src->Size()/inplace_written;
		}

		if (ret
			&& dst->Size()!=src->Size())
		{
			ret = dst->Resize(src->Size());
		}
		
		Server->Log("Wrote "+PrettyPrintBytes(inplace_written)
			+". Skipped "+PrettyPrintBytes(src->Size()-inplace_written)+" ("+
			convert(static_cast<int>(skipped_pc+0.5f))+"%)", LL_INFO);

		return ret;
	}
}

int skiphash_copy_file()
{
	std::string src = Server->getServerParameter("src");
	std::string dst = Server->getServerParameter("dst");
	std::string hashsrc = Server->getServerParameter("hashsrc");

	return skiphash_copy((src),
		(dst),
		(hashsrc)) ? 0 : 1;
}
