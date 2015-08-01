/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include <string>

void unescapeMessage(std::string &msg)
{
	for(size_t i=0;i<msg.size();++i)
	{
		if(msg[i]=='$')
		{
			if(i+1<msg.size() )
			{
				bool ok=false;
				if(msg[i+1]=='$')
				{
					ok=true;
				}
				else if(msg[i+1]=='r')
				{
					ok=true;
					msg[i]='#';
				}

				if(ok)
				{
					msg.erase(msg.begin()+i+1);
				}
			}
		}
	}
}

void escapeClientMessage(std::string &msg)
{
	for(size_t i=0;i<msg.size();++i)
	{
		if(msg[i]=='#' )
		{
			msg[i]='$';
			msg.insert(i+1, "r");
		}
		else if(msg[i]=='$')
		{
			msg.insert(i+1, "$");
			++i;
		}
	}
}

bool testEscape(void)
{
	std::string msg1="Das ist ein # test";
	std::string msg2="Das ist ein test";
	std::string msg3="Das ist ein ## test";
	std::string msg4="##Das ist ein # test##";
	std::string msg5="$$Das ist ein # test$$";
	std::string msg6="Das ist ein $ test";
	escapeClientMessage(msg1);
	if(msg1!="Das ist ein $r test")
		return false;
	escapeClientMessage(msg2);
	escapeClientMessage(msg3);
	escapeClientMessage(msg4);
	if(msg4!="$r$rDas ist ein $r test$r$r")
		return false;
	escapeClientMessage(msg5);
	escapeClientMessage(msg6);
	unescapeMessage(msg1);
	unescapeMessage(msg2);
	unescapeMessage(msg3);
	unescapeMessage(msg4);
	unescapeMessage(msg5);
	unescapeMessage(msg6);
	if(msg1!="Das ist ein # test") return false;
	if(msg2!="Das ist ein test") return false;
	if(msg3!="Das ist ein ## test") return false;
	if(msg4!="##Das ist ein # test##") return false;
	if(msg5!="$$Das ist ein # test$$") return false;
	if(msg6!="Das ist ein $ test") return false;
	return true;
}