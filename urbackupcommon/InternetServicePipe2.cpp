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

#include "InternetServicePipe2.h"
#include "../cryptoplugin/ICryptoFactory.h"
#include "../Interface/Server.h"

extern ICryptoFactory *crypto_fak;

InternetServicePipe2::InternetServicePipe2()
{
	init(NULL, std::string());
}

InternetServicePipe2::InternetServicePipe2( IPipe *cs, const std::string &key )
{
	init(cs, key);
}

InternetServicePipe2::~InternetServicePipe2()
{
	if(destroy_cs)
	{
		delete cs;
	}
}

void InternetServicePipe2::init( IPipe *pcs, const std::string &key )
{
	cs=pcs;
	destroy_cs=false;
	has_error=false;
	curr_write_chunk_size=0;
	last_flush_time=Server->getTimeMS();

	enc.reset(crypto_fak->createAESGCMEncryption(key));
	dec.reset(crypto_fak->createAESGCMDecryption(key));
}

size_t InternetServicePipe2::Read( char *buffer, size_t bsize, int timeoutms/*=-1 */ )
{
	size_t data_size = bsize;
	if(!dec->get(buffer, data_size))
	{
		has_error=true;
		return 0;
	}

	if(data_size>0)
	{
		return data_size;
	}

	int64 starttime=0;

	if(timeoutms>0)
	{
		starttime = Server->getTimeMS();
	}

	do
	{
		size_t read = cs->Read(buffer, bsize, static_cast<int>(timeoutms>0 ? (timeoutms-(Server->getTimeMS()-starttime)) : timeoutms));

		if(read>0)
		{
			if(!dec->put(buffer, read))
			{
				has_error=true;
				return 0;
			}

			data_size=bsize;
			if(!dec->get(buffer, data_size))
			{
				has_error=true;
				return 0;
			}

			if(data_size>0)
			{
				return data_size;
			}			
		}
		else
		{
			return 0;
		}
	}
	while(timeoutms==-1 || (timeoutms>0 && Server->getTimeMS()-starttime<timeoutms));

	return 0;
}

size_t InternetServicePipe2::Read( std::string *ret, int timeoutms/*=-1 */ )
{
	bool l_has_error=false;
	*ret = dec->get(l_has_error);

	if(l_has_error)
	{
		has_error=true;
		return 0;
	}

	if(!ret->empty())
	{
		return ret->size();
	}

	int64 starttime=0;

	if(timeoutms>0)
	{
		starttime = Server->getTimeMS();
	}

	do 
	{
		size_t read = cs->Read(ret, static_cast<int>(timeoutms>0 ? (timeoutms-(Server->getTimeMS()-starttime)) : timeoutms));

		if(read>0)
		{
			if(!dec->put(ret->data(), read))
			{
				has_error=true;
				return 0;
			}

			bool l_has_error=false;
			*ret = dec->get(l_has_error);

			if(l_has_error)
			{
				has_error=true;
				return 0;
			}

			if(!ret->empty())
			{
				return ret->size();
			}			
		}
		else
		{
			return 0;
		}

	} while (timeoutms>0 && Server->getTimeMS()-starttime<timeoutms);	

	return 0;
}

bool InternetServicePipe2::Write( const char *buffer, size_t bsize, int timeoutms/*=-1*/, bool flush/*=true */ )
{
	if(buffer!=NULL)
	{
		curr_write_chunk_size+=bsize;
		enc->put(buffer, bsize);
	}

	if(flush || curr_write_chunk_size>128*1024 || (Server->getTimeMS()-last_flush_time)>200)
	{
		enc->flush();
		curr_write_chunk_size=0;
		last_flush_time=Server->getTimeMS();
	}

	std::string tosend = enc->get();

	if(!tosend.empty())
	{
		return cs->Write(tosend, timeoutms, flush);
	}
	else
	{
		return true;
	}
}

bool InternetServicePipe2::Write( const std::string &str, int timeoutms/*=-1*/, bool flush/*=true */ )
{
	return Write(str.data(), str.size(), timeoutms, flush);
}

bool InternetServicePipe2::Flush(int timeoutms)
{
	return Write(NULL, 0, timeoutms, true);
}

bool InternetServicePipe2::isWritable( int timeoutms/*=0 */ )
{
	return cs->isWritable(timeoutms);
}

bool InternetServicePipe2::isReadable( int timeoutms/*=0 */ )
{
	return dec->hasData() || cs->isReadable(timeoutms);
}

bool InternetServicePipe2::hasError( void )
{
	return cs->hasError() || has_error;
}

void InternetServicePipe2::shutdown( void )
{
	cs->shutdown();
}

size_t InternetServicePipe2::getNumElements( void )
{
	return cs->getNumElements();
}

void InternetServicePipe2::addThrottler( IPipeThrottler *throttler )
{
	cs->addThrottler(throttler);
}

void InternetServicePipe2::addOutgoingThrottler( IPipeThrottler *throttler )
{
	cs->addOutgoingThrottler(throttler);
}

void InternetServicePipe2::addIncomingThrottler( IPipeThrottler *throttler )
{
	cs->addIncomingThrottler(throttler);
}

_i64 InternetServicePipe2::getTransferedBytes( void )
{
	return cs->getTransferedBytes();
}

void InternetServicePipe2::resetTransferedBytes( void )
{
	cs->resetTransferedBytes();
}

std::string InternetServicePipe2::decrypt( const std::string &data )
{
	if(!dec->put(data.data(), data.size()))
	{
		has_error=true;
		return std::string();
	}

	bool l_has_error;
	std::string ret =  dec->get(l_has_error);
	if(l_has_error)
	{
		has_error=true;
		return std::string();
	}

	return ret;
}

std::string InternetServicePipe2::encrypt( const std::string &data )
{
	enc->put(data.data(), data.size());
	enc->flush();
	return enc->get();
}

void InternetServicePipe2::destroyBackendPipeOnDelete( bool b )
{
	destroy_cs = b;
}

void InternetServicePipe2::setBackendPipe( IPipe *pCS )
{
	cs = pCS;
}

IPipe * InternetServicePipe2::getRealPipe()
{
	return cs;
}

int64 InternetServicePipe2::getEncryptionOverheadBytes()
{
	return enc->getOverheadBytes() + dec->getOverheadBytes();
}

