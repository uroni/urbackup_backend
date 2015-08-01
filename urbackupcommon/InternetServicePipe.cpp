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

#include "InternetServicePipe.h"
#include "../cryptoplugin/ICryptoFactory.h"

#include "../Interface/Server.h"
#include <string.h>

extern ICryptoFactory *crypto_fak;

InternetServicePipe::InternetServicePipe(void)
	: cs(NULL), destroy_cs(false)
{
	enc=NULL;
	dec=NULL;
}

InternetServicePipe::InternetServicePipe(IPipe *cs, const std::string &key)
	: cs(cs), destroy_cs(false)
{
	enc=crypto_fak->createAESEncryption(key);
	dec=crypto_fak->createAESDecryption(key);
}

InternetServicePipe::~InternetServicePipe(void)
{
	if(enc!=NULL) enc->Remove();
	if(dec!=NULL) dec->Remove();
	if(destroy_cs && cs!=NULL)
	{
		Server->destroy(cs);
	}
}

void InternetServicePipe::init(IPipe *pcs, const std::string &key)
{
	cs=pcs;
	destroy_cs=false;
	if(enc!=NULL) enc->Remove();
	if(dec!=NULL) dec->Remove();
	enc=crypto_fak->createAESEncryption(key);
	dec=crypto_fak->createAESDecryption(key);
}

size_t InternetServicePipe::Read(char *buffer, size_t bsize, int timeoutms)
{
	size_t rc=cs->Read(buffer, bsize, timeoutms);
	if(rc>0)
	{
		size_t off=dec->decrypt(buffer, rc);
		if(off!=0 )
		{
			if(rc-off>0)
			{
				memmove(buffer, buffer+off, rc-off);
			}
			return rc-off;
		}
		return rc;
	}
	return 0;
}

std::string InternetServicePipe::decrypt(const std::string &data)
{
	return dec->decrypt(data);
}

std::string InternetServicePipe::encrypt(const std::string &data)
{
	return enc->encrypt(data);
}

bool InternetServicePipe::Write(const char *buffer, size_t bsize, int timeoutms, bool flush)
{
	std::string encbuf=enc->encrypt(buffer, bsize);
	bool b=cs->Write(encbuf, timeoutms, flush);
	return b;
}

size_t InternetServicePipe::Read(std::string *ret, int timeoutms)
{
	size_t rc=cs->Read(ret, timeoutms);
	if(rc>0)
	{
		size_t off=dec->decrypt((char*)ret->c_str(), ret->size());
		if(off!=0 )
		{
			if(rc-off>0)
			{
				memmove((char*)ret->c_str(), ret->c_str()+off, rc-off);
				ret->resize(rc-off);
			}
			else
			{
				ret->clear();
			}
			return rc-off;
		}
		return rc;
	}
	return 0;
}

bool InternetServicePipe::Write(const std::string &str, int timeoutms, bool flush)
{
	return Write(str.c_str(), str.size(), timeoutms, flush);
}

/**
* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
*/
bool InternetServicePipe::isWritable(int timeoutms)
{
	return cs->isWritable(timeoutms);
}

bool InternetServicePipe::isReadable(int timeoutms)
{
	return cs->isReadable(timeoutms);
}

bool InternetServicePipe::hasError(void)
{
	return cs->hasError();
}

void InternetServicePipe::shutdown(void)
{
	cs->shutdown();
}

size_t InternetServicePipe::getNumElements(void)
{
	return cs->getNumElements();
}

IPipe *InternetServicePipe::getRealPipe(void)
{
	return cs;
}

void InternetServicePipe::destroyBackendPipeOnDelete(bool b)
{
	destroy_cs=b;
}

void InternetServicePipe::setBackendPipe(IPipe *pCS)
{
	cs=pCS;
}

void InternetServicePipe::addThrottler(IPipeThrottler *throttler)
{
	cs->addThrottler(throttler);
}

void InternetServicePipe::addOutgoingThrottler(IPipeThrottler *throttler)
{
	cs->addOutgoingThrottler(throttler);
}

void InternetServicePipe::addIncomingThrottler(IPipeThrottler *throttler)
{
	cs->addIncomingThrottler(throttler);
}

_i64 InternetServicePipe::getTransferedBytes(void)
{
	return cs->getTransferedBytes();
}

void InternetServicePipe::resetTransferedBytes(void)
{
	cs->resetTransferedBytes();
}

bool InternetServicePipe::Flush(int timeoutms)
{
	return cs->Flush(timeoutms);
}
