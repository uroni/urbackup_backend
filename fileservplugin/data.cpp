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

#include "../vld.h"
#include "data.h"
#ifndef _WIN32
#include <memory.h>
#endif

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
	size_t cpos=data.size();
	data.resize(cpos+sizeof(int) );
	memcpy(&data[cpos],&ta,sizeof(int) );
}

void CWData::addUInt(unsigned int ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(unsigned int) );
	memcpy(&data[cpos],&ta,sizeof(unsigned int) );
}

void CWData::addUInt64(unsigned long long int ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(unsigned long long int) );
	memcpy(&data[cpos],&ta,sizeof(unsigned long long int) );
}

void CWData::addFloat(float ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(float) );
	memcpy(&data[cpos],&ta,sizeof(float) );
}

void CWData::addString(std::string ta)
{
	size_t cpos=data.size();
	data.resize(cpos+sizeof(unsigned int)+ta.size() );
	unsigned int len=(unsigned int)ta.size();
	memcpy(&data[cpos], &len, sizeof(unsigned int) );
	cpos+=sizeof(unsigned int);
	memcpy(&data[cpos],ta.c_str(), ta.size() );
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

CRData::CRData(const char* c,size_t datalength)
{
	data=c;
	streampos=0;
	datalen=datalength;
}

bool CRData::getInt(int *ret)
{
	if(streampos+sizeof(int)>datalen )
	{
		return false;
	}

	memcpy(ret, &data[streampos], sizeof(int) );
	streampos+=sizeof(int);
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

	if(streampos+strlen>datalen )
	{
		return false;
	}

	ret->resize(strlen);
	memcpy((char*)ret->c_str(), &data[streampos], strlen);
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
