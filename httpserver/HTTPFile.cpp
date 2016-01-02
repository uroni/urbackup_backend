/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#include "HTTPFile.h"
#include "MIMEType.h"
#include "IndexFiles.h"

#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../Interface/Pipe.h"

#include "../stringtools.h"

#define FP_READ_SIZE 8192

CHTTPFile::CHTTPFile(std::string pFilename, IPipe *pOutput)
{
	filename=pFilename;
	output=pOutput;
}

std::string CHTTPFile::getContentType(void)
{
	std::string ext=findextension(filename);
	return MIMEType::getMIMEType(ext);
}

void CHTTPFile::operator ()(void)
{
	Server->Log("Sending file \""+filename+"\"", LL_DEBUG);
	IFile *fp=Server->openFile(filename);

	if( fp==NULL )
	{
		const std::vector<std::string> idxf=IndexFiles::getIndexFiles();
		for(size_t i=0;i<idxf.size();++i)
		{
			std::string fn=filename+"/"+idxf[i];
			fp=Server->openFile(fn);
			if( fp!=NULL )
			{
				filename=fn;
				break;
			}
		}
	}

	std::string ct=getContentType();

	if( fp==NULL )
	{
		output->Write("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 22\r\n\r\nSorry. File not found.");
		return;
	}

	std::string status="HTTP/1.1 200 ok\r\n";

	std::string header="Server: CS\r\nContent-Type: "+ct+"\r\nCache-Control: max-age=3600\r\nConnection: Keep-Alive\r\nKeep-Alive: timeout=15, max=95\r\nContent-Length: "+convert(fp->Size())+"\r\n\r\n";

	Server->Log("Sending file: "+filename, LL_INFO);
	output->Write(status+header);
	
	size_t bytes=0;
	std::string buf;
	while( (buf=fp->Read(FP_READ_SIZE)).size()>0 )
	{
		bytes+=buf.size();
		output->Write(buf);
	}

	Server->Log("Sending file: "+filename+" done", LL_INFO);

	Server->destroy(fp);
}
