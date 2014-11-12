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

#include <memory.h>
#include "data.h"
#include "../stringtools.h"


char* CWData::getDataPtr(void)
{
	if(data.size()>0)
		return &data[0];
	else
		return NULL;
}

unsigned long CWData::getDataSize(void)
{
	return (unsigned long)data.size();
}

void CWData::addInt(int ta)
{
	ta=little_endian(ta);
	size_t cpos=data.size();
	data.resize(cpos+sizeof(int) );
	memcpy(&data[cpos],&ta,sizeof(int) );
}

void CWData::addUInt(unsigned int ta)
{
	ta=little_endian(ta);
	size_t cpos=data.size();
	data.resize(cpos+sizeof(unsigned int) );
	memcpy(&data[cpos],&ta,sizeof(unsigned int) );
}

void CWData::addInt64(_i64 ta)
{
	ta=little_endian(ta);
	size_t cpos=data.size();
	data.resize(cpos+sizeof(_i64) );
	memcpy(&data[cpos],&ta,sizeof(_i64) );
}

void CWData::addUInt64(uint64 ta)
{
	ta=little_endian(ta);
	size_t cpos=data.size();
	data.resize(cpos+sizeof(uint64) );
	memcpy(&data[cpos],&ta,sizeof(uint64) );
}

void CWData::addFloat(float ta)
{
	ta=little_endian(ta);
	size_t cpos=data.size();
	data.resize(cpos+sizeof(float) );
	memcpy(&data[cpos],&ta,sizeof(float) );
}

void CWData::addUShort(unsigned short ta)
{
	ta=little_endian(ta);
	size_t cpos=data.size();
	data.resize(cpos+sizeof(unsigned short) );
	memcpy(&data[cpos],&ta,sizeof(unsigned short) );
}	

void CWData::addString(std::string ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(unsigned int)+ta.size() );
	unsigned int len=little_endian((unsigned int)ta.size());
	memcpy(&data[cpos], &len, sizeof(unsigned int) );
	cpos+=sizeof(unsigned int);
	if(!ta.empty())
	{
		memcpy(&data[cpos],ta.c_str(), ta.size() );
	}
}

void CWData::addChar(char ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(char) );
	data[cpos]=ta;
}

void CWData::addUChar(unsigned char ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(unsigned char) );
	data[cpos]=ta;
}

void CWData::addVoidPtr(void* ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(void*) );
	memcpy(&data[cpos],&ta,sizeof(void*) );
}

void CWData::addBuffer(const char* buffer, size_t bsize)
{
	size_t cpos=data.size();
	data.resize(cpos+bsize);
	memcpy(&data[cpos], buffer, bsize);
}

void CWData::clear()
{
	data.clear();
}

CRData::CRData(const char* c,size_t datalength, bool pCopy)
{
	data=NULL;
	set(c,datalength, pCopy);
}

CRData::CRData(void)
{
	data=NULL;
	streampos=0;
	datalen=0;
}

void CRData::set(const char* c,size_t datalength, bool pCopy)
{
	copy=pCopy;
	if( copy==false )
	{
		data=c;
	}
	else
	{
		if( data!=NULL )
			delete [] data;
		data=new char[datalength];
		memcpy((void*)data, c, datalength);
	}
	streampos=0;
	datalen=datalength;
}

CRData::CRData(const std::string *str)
{
	set(str->c_str(), str->size(), false);
}

CRData::~CRData()
{
	if( copy==true )
		delete []data;
}

bool CRData::getInt(int *ret)
{
	if(streampos+sizeof(int)>datalen )
	{
		return false;
	}

	memcpy(ret, &data[streampos], sizeof(int) );
	streampos+=sizeof(int);
	*ret=little_endian(*ret);
	return true;
}

bool CRData::getInt64(_i64 *ret)
{
	if(streampos+sizeof(_i64)>datalen )
	{
		return false;
	}

	memcpy(ret, &data[streampos], sizeof(_i64) );
	streampos+=sizeof(_i64);
	*ret=little_endian(*ret);
	return true;
}

bool CRData::getUInt(unsigned int *ret)
{
	if(streampos+sizeof(unsigned int )>datalen )
	{
		return false;
	}

	memcpy(ret, &data[streampos], sizeof(unsigned int ) );
	streampos+=sizeof(unsigned int);
	*ret=little_endian(*ret);
	return true;
}
	
bool CRData::getFloat(float *ret)
{
	if(streampos+sizeof(float)>datalen )
	{
		return false;
	}

	memcpy(ret, &data[streampos], sizeof(float) );
	streampos+=sizeof(float);
	*ret=little_endian(*ret);
	return true;
}

bool CRData::getUShort( unsigned short *ret)
{
	if(streampos+sizeof(unsigned short)>datalen )
	{
		return false;
	}

	memcpy(ret, &data[streampos], sizeof(unsigned short) );
	streampos+=sizeof(unsigned short);
	*ret=little_endian(*ret);
	return true;	
}

bool CRData::getStr(std::string *ret)
{
	if(streampos+sizeof(unsigned int)>datalen )
	{
		return false;
	}

	unsigned int strlen;
	memcpy(&strlen,&data[streampos], sizeof(unsigned int) );
	streampos+=sizeof(unsigned int);

	strlen = little_endian(strlen);

	if(streampos+strlen>datalen )
	{
		return false;
	}

	if(strlen>0)
	{
		ret->resize(strlen);
		memcpy((char*)ret->c_str(), &data[streampos], strlen);
	}
	else
	{
		ret->clear();
	}
	streampos+=strlen;
	return true;
}

bool CRData::getChar(char *ret)
{
	if(streampos+sizeof(char)>datalen )
	{
		return false;
	}

	(*ret)=data[streampos];
	streampos+=sizeof(char);

	return true;
}

bool CRData::getUChar(unsigned char *ret)
{
	if(streampos+sizeof(unsigned char)>datalen )
	{
		return false;
	}

	(*ret)=data[streampos];
	streampos+=sizeof(unsigned char);

	return true;
}

bool CRData::getVoidPtr(void **ret)
{
	if(streampos+sizeof(void*)>datalen )
	{
		return false;
	}

	memcpy(ret, &data[streampos], sizeof(void*) );
	streampos+=sizeof(void*);
	return true;
}

unsigned int CRData::getSize(void)
{
	return (unsigned int)datalen;
}

unsigned int CRData::getLeft(void)
{
	return (unsigned int)(datalen-streampos);
}

unsigned int CRData::getStreampos(void)
{
	return (unsigned int)streampos;
}

const char *CRData::getDataPtr(void)
{
	return data;
}

const char *CRData::getCurrDataPtr(void)
{
	return data+streampos;
}

void CRData::setStreampos(unsigned int spos)
{
	if( spos <= datalen )
	{
		streampos=spos;
	}
}

bool CRData::incrementPtr(unsigned int amount)
{
	if((unsigned int)amount>getLeft())
		return false;

	streampos+=amount;
	return true;
}

