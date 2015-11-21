#include "skiphash_copy.h"
#include <string>
#include "../../urbackupcommon/chunk_hasher.h"
#include "../../stringtools.h"
#include "../../urbackupcommon/os_functions.h"

bool skiphash_copy(const std::wstring& src_path,
	const std::wstring& dst_path,
	const std::wstring& hashinput_path)
{
	std::auto_ptr<IFile> src(Server->openFile(os_file_prefix(src_path), MODE_READ_SEQUENTIAL));

	if(!src.get())
	{
		Server->Log(L"Could not open source file for reading at \"" + src_path+L"\"", LL_ERROR);
		return false;
	}

	std::auto_ptr<IFile> hashoutput(Server->openFile(os_file_prefix(dst_path+L".hash"), MODE_WRITE));

	if(!hashoutput.get())
	{
		Server->Log(L"Could not open hash output for writing at \""+dst_path+L".hash\"", LL_ERROR);
		return false;
	}

	bool dst_exists = Server->fileExists(dst_path);

	std::auto_ptr<IFile> dst(Server->openFile(os_file_prefix(dst_path), MODE_RW));

	if(!dst.get())
	{
		Server->Log(L"Could not open output file for writing at \""+dst_path+L"\"", LL_ERROR);
		return false;
	}

	std::auto_ptr<IFile> hashinput;
	
	if(!hashinput_path.empty())
	{
		hashinput.reset(Server->openFile(os_file_prefix(hashinput_path), MODE_READ));

		if(!hashinput.get())
		{
			Server->Log(L"Could not open hash input file at \""+hashinput_path+L"\"", LL_ERROR);
			return false;
		}
	}

	if(!dst_exists || !hashinput.get())
	{
		Server->Log("Destination file does not exist or there is no hash input. Performing full copy (with hash output)...", LL_INFO);
		return build_chunk_hashs(src.get(), hashoutput.get(), NULL, false, dst.get(), false)!="";
	}
	else
	{
		int64 inplace_written = 0;
		bool ret = build_chunk_hashs(src.get(), hashoutput.get(), NULL, false,
			dst.get(), true, &inplace_written, hashinput.get())!="";
		float skipped_pc=0;
		if(src->Size()>0)
		{
			skipped_pc = 100.f - 100.f*src->Size()/inplace_written;
		}
		
		Server->Log("Wrote "+PrettyPrintBytes(inplace_written)
			+". Skipped "+PrettyPrintBytes(src->Size()-inplace_written)+" ("+
			nconvert(static_cast<int>(skipped_pc+0.5f))+"%)", LL_INFO);

		return ret;
	}
}

int skiphash_copy_file()
{
	std::string src = Server->getServerParameter("src");
	std::string dst = Server->getServerParameter("dst");
	std::string hashsrc = Server->getServerParameter("hashsrc");

	return skiphash_copy(Server->ConvertToUnicode(src),
		Server->ConvertToUnicode(dst),
		Server->ConvertToUnicode(hashsrc)) ? 1 : 0;
}
