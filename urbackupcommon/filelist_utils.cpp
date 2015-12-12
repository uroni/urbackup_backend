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

#include "filelist_utils.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

void writeFileRepeat(IFile *f, const char *buf, size_t bsize)
{
	_u32 written=0;
	do
	{
		_u32 rc=f->Write(buf+written, (_u32)(bsize-written));
		written+=rc;
		if(rc==0)
		{
			Server->Log("Failed to write to file "+f->getFilename()+" retrying...", LL_WARNING);
			Server->wait(10000);
		}
	}
	while(written<bsize );
}

void writeFileRepeat(IFile *f, const std::string &str)
{
	writeFileRepeat(f, str.c_str(), str.size());
}

std::string escapeListName( const std::string& listname )
{
	std::string ret;
	ret.reserve(listname.size());
	for(size_t i=0;i<listname.size();++i)
	{
		if(listname[i]=='"')
		{
			ret+="\\\"";
		}
		else if(listname[i]=='\\')
		{
			ret+="\\\\";
		}
		else
		{
			ret+=listname[i];
		}
	}
	return ret;
}

void writeFileItem(IFile* f, SFile cf)
{
	if(cf.isdir)
	{
		writeFileRepeat(f, "d\""+escapeListName(Server->ConvertToUTF8(cf.name))+"\" 0 "+nconvert(cf.last_modified)+"\n");
	}
	else
	{
		writeFileRepeat(f, "f\""+escapeListName(Server->ConvertToUTF8(cf.name))+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)+"\n");
	}
}

void writeFileItem(IFile* f, SFile cf, std::string extra)
{
	if(!extra.empty() && extra[0]=='&') extra[0]='#';

	if(cf.isdir)
	{
		writeFileRepeat(f, "d\""+escapeListName(Server->ConvertToUTF8(cf.name))+"\" 0 "+nconvert(cf.last_modified)
			+extra+"\n");
	}
	else
	{
		

		writeFileRepeat(f, "f\""+escapeListName(Server->ConvertToUTF8(cf.name))+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)
			+extra+"\n");
	}
}


bool FileListParser::nextEntry( char ch, SFile &data, std::map<std::wstring, std::wstring>* extra )
{
	switch(state)
	{
	case ParseState_Type:
		if(ch=='f')
			data.isdir=false;
		else if(ch=='d')
			data.isdir=true;
		else
			Server->Log("Error parsing file BackupServerGet::getNextEntry - 1", LL_ERROR);
		state=ParseState_Quote;
		break;
	case ParseState_Quote:
		//"
		state=ParseState_Name;
		break;
	case ParseState_NameFinish:
		if(ch!='"')
		{
			t_name.erase(t_name.size()-1,1);
			data.name=Server->ConvertToUnicode(t_name);
			t_name="";
			if(data.isdir)
			{
				if(ch=='\n')
				{
					reset();
					if(extra!=NULL)
					{
						extra->clear();
					}
					return true;
				}
				else
				{
					state=ParseState_ExtraParams;
					return false;
				}
			}
			else
			{
				state=ParseState_Filesize;
			}
		}
		//fallthrough
	case ParseState_Name:
		if(state==ParseState_Name && ch=='"')
		{
			state=ParseState_NameFinish;
		}
		else if(state==ParseState_Name && ch=='\\')
		{
			state=ParseState_NameEscape;
			break;
		}
		else if(state==ParseState_NameFinish)
		{
			state=ParseState_Name;
		}

		t_name+=ch;
		break;
	case ParseState_NameEscape:
		if(ch!='"' && ch!='\\')
		{
			t_name+='\\';
		}
		t_name+=ch;
		state=ParseState_Name;
		break;
	case ParseState_Filesize:
		if(ch!=' ')
		{
			t_name+=ch;
		}
		else
		{
			data.size=os_atoi64(t_name);
			t_name="";
			state=ParseState_ModifiedTime;
		}
		break;
	case ParseState_ModifiedTime:
		if(ch!='\n' && ch!='#')
		{
			t_name+=ch;
		}
		else
		{
			data.last_modified=os_atoi64(t_name);
			if(ch=='\n')
			{
				reset();
				if(extra!=NULL)
				{
					extra->clear();
				}
				return true;
			}
			else
			{
				t_name="";
				state=ParseState_ExtraParams;
			}
		}
		break;
	case ParseState_ExtraParams:
		if(ch!='\n')
		{
			t_name+=ch;
		}
		else
		{
			if(extra!=NULL)
			{
				extra->clear();
				ParseParamStrHttp(t_name, extra, false);
			}
			reset();
			return true;
		}
		break;
	}
	return false;
}

void FileListParser::reset( void )
{
	t_name="";
	state=ParseState_Type;
}

FileListParser::FileListParser()
	: state(ParseState_Type)
{

}
