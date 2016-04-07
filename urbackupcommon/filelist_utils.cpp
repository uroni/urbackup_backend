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
			Server->Log("Failed to write to file "+f->getFilename()+" retrying... "+os_last_error_str(), LL_WARNING);
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

void writeFileItem(IFile* f, SFile cf, size_t* written, size_t* change_identicator_off)
{
	if(cf.isdir)
	{
		if(cf.name!="..")
		{
			std::string towrite = "d\""+escapeListName((cf.name))+"\" 0 ";

			if(change_identicator_off!=NULL)
			{
				*change_identicator_off=towrite.size();
			}

			towrite+=convert(cf.last_modified)+"\n";

			writeFileRepeat(f, towrite);

			if(written!=NULL)
			{
				*written+=towrite.size();
			}
		}
		else
		{
			std::string towrite="u\n";
			
			writeFileRepeat(f, towrite);

			if(written!=NULL)
			{
				*written+=towrite.size();
			}
		}		
	}
	else
	{
		std::string towrite = "f\""+escapeListName((cf.name))+"\" "+convert(cf.size)+" ";

		if(change_identicator_off!=NULL)
		{
			*change_identicator_off=towrite.size();
		}

		towrite+=convert(cf.last_modified)+"\n";

		writeFileRepeat(f, towrite);

		if(written!=NULL)
		{
			*written+=towrite.size();
		}
	}
}

void writeFileItem(IFile* f, SFile cf, std::string extra)
{
	if(!extra.empty() && extra[0]=='&') extra[0]='#';

	if(cf.isdir)
	{
		if(cf.name!="..")
		{
			writeFileRepeat(f, "d\""+escapeListName((cf.name))+"\" 0 "+convert(cf.last_modified)
				+extra+"\n");
		}
		else
		{
			writeFileRepeat(f, "u\n");
		}
		
	}
	else
	{
		writeFileRepeat(f, "f\""+escapeListName((cf.name))+"\" "+convert(cf.size)+" "+convert(cf.last_modified)
			+extra+"\n");
	}
}


bool FileListParser::nextEntry( char ch, SFile &data, std::map<std::string, std::string>* extra )
{
	++pos;
	switch(state)
	{
	case ParseState_Type:
		if(ch=='f')
		{
			data.isdir=false;
			state=ParseState_Quote;
		}
		else if(ch=='d')
		{
			data.isdir=true;
			state=ParseState_Quote;
		}
		else if(ch=='u')
		{
			data.isdir=true;
			state=ParseState_TypeFinish;
		}
		else
		{
			Server->Log("Error parsing file BackupServerGet::getNextEntry - 1. Unexpected char '"+std::string(1, ch)+"' at pos "+convert(pos-1)+". Expected 'f', 'd' or 'u'.", LL_ERROR);
		}
		break;
	case ParseState_TypeFinish:
		if(ch=='\n')
		{
			data.name="..";
			data.last_modified=0;
			data.size = 0;
			reset();
			if(extra!=NULL)
			{
				extra->clear();
			}
			return true;
		}
		else
		{
			Server->Log("Error parsing file BackupServerGet::getNextEntry - 2. Unexpected char '" + std::string(1, ch) + "'at pos " + convert(pos-1)+". Expected '\\n'.", LL_ERROR);
		}
		break;
	case ParseState_Quote:
		//"
		state=ParseState_Name;
		break;
	case ParseState_NameFinish:
		if(ch!='"')
		{
			t_name.erase(t_name.size()-1,1);
			data.name=(t_name);
			t_name="";
			if(data.isdir && data.name=="..")
			{
				data.last_modified=0;
				data.size = 0;

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
			else if(data.isdir && ch=='\n')
			{
				data.last_modified=0;
				data.size = 0;

				reset();
				if(extra!=NULL)
				{
					extra->clear();
				}
				return true;
			}
			else
			{
				state=ParseState_Filesize;
				return false;
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
	pos = 0;
}

FileListParser::FileListParser()
	: state(ParseState_Type), pos(0)
{

}
