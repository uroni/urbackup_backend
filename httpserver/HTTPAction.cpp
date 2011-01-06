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

#include "HTTPAction.h"

#include "../Interface/Server.h"
#include "../Interface/Pipe.h"

#include "../stringtools.h"

CHTTPAction::CHTTPAction(const std::wstring &pName, const std::wstring pContext, const std::string &pGETStr, const std::string pPOSTStr, const str_nmap &pRawPARAMS, IPipe *pOutput)
{
	name=pName;
	GETStr=pGETStr;
	POSTStr=pPOSTStr;
	RawPARAMS=pRawPARAMS;
	output=pOutput;
	context=pContext;
}

#define MAP(x,y) { std::map<std::string, std::string>::iterator iter=RawPARAMS.find(x); if(iter!=RawPARAMS.end() ) PARAMS.insert(std::pair<std::string, std::string>(y, iter->second) ); }

void CHTTPAction::operator()(void)
{
	std::map<std::wstring, std::wstring> GET;
	std::map<std::wstring, std::wstring> POST;
	ParseParamStr(GETStr, &GET);
	ParseParamStr(POSTStr, &POST);

	std::map<std::string, std::string> PARAMS;

	MAP("POSTFILEKEY","POSTFILEKEY");
	MAP("ACCEPT-LANGUAGE", "ACCEPT_LANGUAGE");

	std::string op=Server->Execute(name, context, GET, POST, PARAMS);

	if( op.empty() )
	{
		output->Write("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 22\r\n\r\nSorry. File not found.");
		return;
	}

	size_t content_length=op.size()-(op.find("\r\n\r\n")+4);

	op="HTTP/1.1 200 ok\r\nCache-Control: max-age=0\r\nContent-Length: "+nconvert(content_length)+"\r\n"+op;

	output->Write(op);
}