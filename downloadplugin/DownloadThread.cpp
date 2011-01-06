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

//---------------------------------------------------------------------------

#include "DownloadThread.h"


DownloadThread::DownloadThread()
{
}
//---------------------------------------------------------------------------
void DownloadThread::operator()(void)
{
        bool b=DownloadfileThreaded(url, filename, pipe, proxy, proxyport);

        CWData d;
		d.addUChar(DL2_DONE);
        if( b==true )
                d.addInt(1);
        else
                d.addInt(0);
                
		pipe->Write(d.getDataPtr(), d.getDataSize());
}
//---------------------------------------------------------------------------

void DownloadThread::init(std::string pUrl,std::string pFilename, IPipe *pPipe, std::string pProxy, unsigned short pProxyport)
{
        url=pUrl;
        filename=pFilename;
        pipe=pPipe;
        proxy=pProxy;
        proxyport=pProxyport;
}
