/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include "vld.h"
#include "Template.h"
#include "stringtools.h"
#include "Table.h"
#include "utf8/utf8.h"
#include "Server.h"

#include "Interface/Database.h"

CTemplate::CTemplate(std::string pFile)
{
	file=pFile;
	std::string tdata=getFile(pFile);

	//if( utf8::is_bom(tdata.begin()) )
	{
		try
		{
			utf8::utf8to16(tdata.begin(), tdata.end(), back_inserter(data) );
		}
		catch(...)
		{
			data=L"Invalid UTF-8";
		}
	}
	/*else
	{
		data=widen(tdata);
	}*/

	mCurrValues=new CTable();
	mValuesRoot=mCurrValues;

	AddDefaultReplacements();
}

CTemplate::~CTemplate()
{
	delete mValuesRoot;

	for(size_t i=0;i<mTables.size();++i)
	{
		mTables[i].first->destroyQuery( mTables[i].second );
	}
}

void CTemplate::AddDefaultReplacements(void)
{
	#define ADD_REPLACEMENT(key,value) mReplacements.push_back( std::pair<std::string, std::string>(key,value) )
	#define ADD_REPLACEMENT_CODE(key,value) {unsigned char ch=key; std::string tmp;tmp+=ch; mReplacements.push_back( std::pair<std::string, std::string>(tmp,value) );}
	/*ADD_REPLACEMENT(L"ä",L"\\xE4");
	ADD_REPLACEMENT(L"ö",L"\\xF6");
	ADD_REPLACEMENT(L"ü",L"\\xFC");
	ADD_REPLACEMENT(L"Ä",L"\\xC4");
	ADD_REPLACEMENT(L"Ö",L"\\xFC");
	ADD_REPLACEMENT(L"Ü",L"\\xD6");
	ADD_REPLACEMENT(L"ß",L"\\xDF");
	ADD_REPLACEMENT_CODE(246, "\\xF6");
	ADD_REPLACEMENT_CODE(228, "\\xE4");
	ADD_REPLACEMENT_CODE(252, "\\xFC");
	ADD_REPLACEMENT_CODE(196, "\\xC4");
	ADD_REPLACEMENT_CODE(214, "\\xD6");
	ADD_REPLACEMENT_CODE(220, "\\xDC");
	ADD_REPLACEMENT_CODE(223, "\\xDF");*/
}

ITable* CTemplate::createTableRecursive(std::wstring key)
{
	std::vector<std::wstring> toks;
	Tokenize(key, toks, L".");
	ITable *ct=mCurrValues;
	for(size_t i=0;i<toks.size();++i)
	{
		ITable *nt=ct->getObject(toks[i]);
		if( nt==NULL )
		{
			nt=new CRATable();
			ct->addObject(toks[i], nt);
		}
		ct=nt;
	}

	return ct;
}

ITable* CTemplate::getTable(std::wstring key)
{
	ITable *ret;
	if( (ret=findTable(key))!=NULL )
		return ret;
	else
		return createTableRecursive(key);
}

void CTemplate::setValue(std::wstring key, std::wstring value)
{
	std::vector<std::wstring> toks;
	Tokenize(key, toks, L".");
	ITable *ct=mCurrValues;
	if( toks.size()==0 )
		return;

	if( toks.size()>1 )
	{
		for(size_t i=0;i<toks.size()-1;++i)
		{
			ct=ct->getObject(toks[i]);
			if( ct==NULL )
				break;
		}
	}

	if( ct!=NULL )
		ct->addString(toks[toks.size()-1], value);
}

ITable *CTemplate::findTable(std::wstring key)
{
	std::vector<std::wstring> toks;
	Tokenize(key, toks, L".");
	ITable *ct;

	if( toks.size()>0 )
	{
		ct=mCurrValues->getObject(toks[0]);
		if( ct==NULL && mValuesRoot!=mCurrValues )
		{
			ct=mValuesRoot->getObject(toks[0]);			
		}

		for(size_t i=1;i<toks.size() && ct!=NULL;++i)
		{
			ct=ct->getObject(toks[i]);
		}

		return ct;
	}
	return NULL;
}


bool CTemplate::FindValue(const std::wstring &key, std::wstring &value, bool dbs)
{
	ITable *val=findTable(key);

	if( val!=NULL )
	{
		value=val->getValue();
		return true;
	}
	else
	{
		if( dbs==false )
			return false;
		else
		{
			for(size_t i=0;i<mTables.size();++i)
			{
				mTables[i].second->Bind(key);
				db_results res=mTables[i].second->Read();
				mTables[i].second->Reset();

				if( res.size() >0 )
				{
					db_single_result result=res[0];
					db_single_result::iterator iter=result.find(L"value");
					if( iter!= result.end() )
					{
						value=iter->second;
						return true;
					}
				}
			}
			return false;
		}
	}
}

std::string CTemplate::getData(void)
{
	std::wstring output=data;
	transform(output);
	std::string ret;
	try
	{
	    if( sizeof(wchar_t)==2 )
        	utf8::utf16to8(output.begin(), output.end(), back_inserter(ret) );
    	    else
    		utf8::utf32to8(output.begin(), output.end(), back_inserter(ret) );
    	}
    	catch(...)
    	{
    	    Server->Log("Invalid UTF8!", LL_ERROR);
    	}
    		
	return ret;
}

void CTemplate::transform(std::wstring &output)
{
	for(size_t i=0;i<output.size();++i)
	{
		if( output[i]=='\n' && i!=0 && output[i-1]!='\r' )
		{
			output.insert(i,L"\r");
		}
	}
	
	bool retry=true;
	while(retry==true)
	{
		retry=false;
		for(size_t i=0;i<output.size();++i)
		{
			// VALUES
			if( next(output, i, L"#{")==true )
			{
				size_t j;
				for(j=i+2;j<output.size();++j)
				{
					if( output[j]=='}' )
						break;
				}

				std::wstring var=output.substr(i+2, (j)-(i+2) );
				std::wstring value;

				bool b=FindValue( var, value);
				if( b==true )
				{
					output.replace(i, j-i+1, value );
				}
				else
				{
					
				}
			}
			//PREPROCESSOR
			{ 
				if( ((i==0||i==1) && next(output, i, L"#exit" )) || (i!=0 && next(output, i, L"\n#exit")) )
				{
					output.erase(i);
					break;
				}
				bool foreach1=false,foreach2=false;
				if( ((i==0||i==1) && (foreach1=next(output, i, L"#foreach in ")==true)) ||  (foreach2=next(output, i, L"\r\n#foreach in "))==true)
				{
					if( foreach2 )
						i+=2;
					std::wstring var;
					for(size_t j=i+12;j<output.size();++j)
					{
						if( output[j]=='\n' || output[j]=='\r' )
							break;

						var+=output[j];
					}

					ITable *tab=findTable(var);

					if( tab!=NULL )
					{
						size_t len=12+var.size();

						size_t end=std::string::npos;
						int count=0;
						for(size_t j=i+len;j<output.size();++j)
						{
							if( next(output, j, L"\r\n#foreach") )
							{
								++count;
							}
							if( next(output, j, L"\r\n#endfor" ) )
							{
								if( count<=0 )
								{
									end=j;
									break;
								}
								else
								{
									--count;
								}
							}
						}

						

						if( foreach2 )
						{
							len+=2;
							i-=2;
						}

						if( end!=std::string::npos )
						{
							std::wstring mid=output.substr(i+len, end-(i+len));
							std::wstring strend=output.substr(end+9);
							output=output.substr(0,i);
							ITable *old_values=mCurrValues;

							for(size_t k=0;k<tab->getSize();++k)
							{
								std::wstring tmp=mid;
								mCurrValues=tab->getObject(k);
								transform(tmp);
								output+=tmp;
							}

							mCurrValues=old_values;

							i=output.size()-1;
							output+=strend;
						}
					}
				}
				bool ifndef1=false,ifndef2=false;
				bool next2=false;
				if( ((i==0||i==1) && next(output, i, L"#ifdef ")==true) || (next2=next(output, i, L"\r\n#ifdef "))==true 
					||((i==0||i==1) && (ifndef1=next(output, i, L"#ifndef "))==true) || ((ifndef2=next(output, i, L"\r\n#ifndef "))==true)
					)
				{
					bool ifndef=false;
					if( ifndef1 || ifndef2 )
						ifndef=true;

					if( next2==true||ifndef2==true )
						i+=2;

					std::wstring var;
					int addi=7;
					if( ifndef==true )
						++addi;
					for(size_t j=i+addi;j<output.size();++j)
					{
						if( output[j]=='\n' || output[j]=='\r' )
							break;

						var+=output[j];
					}

					std::wstring value;

					bool found=FindValue(var, value);

					if( ifndef==true )
						found=!found;

					if( found==true )
					{
						if(i!=0)
							i-=2;

						output.erase(i,addi+var.size()+2 );

						size_t j;
						size_t todel=std::string::npos;
						size_t proc=0;
						for(j=i;j<output.size();++j)
						{
							if( next(output, j , L"\r\n#ifdef") ||  next(output, j , L"\r\n#ifndef") )
							{
								++proc;
							}
							else if( proc==0 && next(output, j, L"\r\n#elseif" ) )
							{
								if( todel==std::string::npos )
									todel=j;
							}
							else if( proc==0 && next(output, j, L"\r\n#else" ) )
							{
								if( todel==std::string::npos )
									todel=j;
							}
							else if( next(output, j, L"\r\n#endif" ) )
							{
								if( proc!=0 )
								{
									--proc;
								}
								else
									break;
							}
						}
						//Not tested...
						if( i>0 )--i;
						output.erase(j, 8);
						if( todel!=std::string::npos )
							output.erase(todel, j-todel);
					}
					else
					{
						size_t todel=0;
						size_t enterelseif=std::string::npos;
						size_t stopelseif=std::string::npos;
						size_t proc=0;
						for(size_t j=i;j<output.size();++j)
						{
							if( next(output, j , L"\r\n#ifdef") ||  next(output, j , L"\r\n#ifndef") )
							{
								++proc;
							}
							else if( proc==0 && next(output, j , L"\r\n#elseif" ) )
							{
								std::wstring key;
								for(size_t k=j+10;k<output.size();++k)
								{
									if( output[k]=='\r' || output[k]=='\n' )
										break;
									key+=output[k];
								}
								if( enterelseif==std::string::npos )
								{
									std::wstring value;
									if( FindValue( key, value) )
									{
										enterelseif=j;
									}
								}
								else if(stopelseif==std::string::npos )
								{
									stopelseif=j;
								}
								output.erase(j, 10+key.size() );
							}
							else if( proc==0 &&next(output, j, L"\r\n#else") )
							{
								if( enterelseif==std::string::npos )
								{
									enterelseif=j;
								}
								else if( stopelseif==std::string::npos )
									stopelseif=j;

								output.erase(j, 7 );
								--j;
								if( todel>0 )
									todel--;
							}
							else if( next(output, j, L"\r\n#endif" ) )
							{
								if( proc!=0 )
									--proc;
								else
								{
									if( enterelseif!=std::string::npos )
										if( stopelseif==std::string::npos )
											stopelseif=j;
									break;
								}
							}
							++todel;
						}
						std::wstring data;
						if( enterelseif!=std::string::npos )
							data=output.substr(enterelseif, stopelseif-enterelseif);
						if( i==0 && data.size()>1 )
							data.erase(0,2);
						if( i!=0 )
						{
							i-=2;
							todel+=2;
						}
						output.erase(i, todel+8);
						if( data.size()>0 )
						{
							output.insert(i, data);
						}
						if( i>0 )
							--i;
						else
						{
							retry=true;
							break;
						}
					}
				}
				if( next(output, i, L"#include \"")==true )
				{
					std::wstring fn;
					bool inside=false;
					for(size_t j=i;j<output.size();++j)
					{
						if( output[j]=='"' && inside==true )
							break;
						else if(output[j]=='"')
							inside=true;
						else if( inside==true )
							fn+=output[j];					
					}

					output.erase(i,11+fn.size() );
					std::wstring ttext=getFileUTF8(ExtractFilePath(file)+"/"+wnarrow(fn) );
					transform(ttext);
					output.insert(i, ttext );
				}				
			}
			//REPLACEMENTS
			for( size_t j=0;j<mReplacements.size();++j)
			{
				if( next(output, i, mReplacements[j].first)==true )
				{
					output.erase(i, mReplacements[j].first.size() );
					output.insert(i, mReplacements[j].second );
					i+=mReplacements[j].second.size();
				}
			}
		}
	}
}

void CTemplate::Reset(void)
{
	delete mValuesRoot;
	mCurrValues=new CTable();
	mValuesRoot=mCurrValues;
}

void CTemplate::addValueTable( IDatabase* db, const std::string &table)
{
	mTables.push_back( std::pair< IDatabase*, IQuery*>(db, db->Prepare("SELECT * FROM "+table+" WHERE key=?",false) ) );
}
