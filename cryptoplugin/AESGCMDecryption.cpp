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
#include "AESGCMDecryption.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

const size_t iv_size = 12;
const size_t end_marker_zeros=2;

AESGCMDecryption::AESGCMDecryption( const std::string &password, bool hash_password )
	: decryption(), decryption_filter(decryption), iv_done(false), end_marker_state(0),
	overhead_bytes(0)
{
	if(hash_password)
	{
		m_sbbKey.resize(CryptoPP::SHA256::DIGESTSIZE);
		CryptoPP::SHA256().CalculateDigest(m_sbbKey, (byte*)password.c_str(), password.size() );
	}
	else
	{
		m_sbbKey.resize(password.size());
		memcpy(m_sbbKey.BytePtr(), password.c_str(), password.size());
	}

	assert(decryption.CanUseStructuredIVs());
	assert(decryption.IsResynchronizable());
}

bool AESGCMDecryption::put( const char *data, size_t data_size)
{
	if(!iv_done)
	{
		size_t toread = (std::min)(iv_size-iv_buffer.size(), data_size);
		iv_buffer.insert(iv_buffer.end(), data, data+toread);

		data_size-=toread;
		data+=toread;

		if(iv_buffer.size()==iv_size)
		{
			overhead_bytes+=iv_buffer.size();
			decryption.SetKeyWithIV(m_sbbKey.BytePtr(), m_sbbKey.size(),
				reinterpret_cast<const byte*>(iv_buffer.data()), iv_buffer.size());
			iv_done=true;
		}

		if(data_size==0)
		{
			return true;
		}
		else if(iv_buffer.size()!=iv_size)
		{
			return false;
		}
	}

	try
	{
		std::string data_copy;
		bool has_error=false;
		bool has_copy=false;

		size_t carry_zeros=0;
		if(end_marker_state>0)
		{
			carry_zeros = end_marker_state;
		}

		size_t escaped_zeros=0;
		size_t end_marker_pos = findAndUnescapeEndMarker(data, data_size, data_copy,
			has_copy, has_error, escaped_zeros);

		if(end_marker_pos==std::string::npos)
		{
			if(has_error)
			{
				Server->Log("Error scanning encrypted data for end marker", LL_ERROR);
				return false;
			}
			else
			{
				if(end_marker_state>0 )
				{
					if(data_size-escaped_zeros>=end_marker_state)
					{
						data_size-=end_marker_state;
					}
					else if(data_size-escaped_zeros>=end_marker_state-carry_zeros)
					{
						data_size-=end_marker_state-carry_zeros;
						carry_zeros=0;
					}
				}

				if(carry_zeros>0)
				{
					for(size_t i=0;i<carry_zeros;++i)
						decryption_filter.Put(0);
				}

				if(has_copy && data_size-escaped_zeros>0)
				{
					decryption_filter.Put(reinterpret_cast<const byte*>(data_copy.data()), data_size-escaped_zeros);
				}
				else if(data_size>0)
				{
					decryption_filter.Put(reinterpret_cast<const byte*>(data), data_size);
				}

				Server->Log("Data without end: "+convert(data_size));
			}
		}
		else
		{
			if(end_marker_pos>end_marker_zeros+1)
			{
				if(carry_zeros>0)
				{
					for(size_t i=0;i<carry_zeros;++i)
						decryption_filter.Put(0);
				}

				if(has_copy)
				{
					decryption_filter.Put(reinterpret_cast<const byte*>(data_copy.data()), end_marker_pos-end_marker_zeros-1);
				}
				else
				{
					decryption_filter.Put(reinterpret_cast<const byte*>(data), end_marker_pos-end_marker_zeros-1);
				}
			}
			try
			{
				Server->Log("Message end. Size: "+convert(decryption_filter.MaxRetrievable()));
				decryption_filter.MessageEnd();
			}
			catch (CryptoPP::Exception&)
			{
				return false;
			}
			
			overhead_bytes+=16; //tag size

			CryptoPP::IncrementCounterByOne(reinterpret_cast<byte*>(&iv_buffer[0]), static_cast<unsigned int>(iv_buffer.size()));
			decryption.Resynchronize(reinterpret_cast<const byte*>(iv_buffer.data()), static_cast<int>(iv_buffer.size()));

			if(data_size>end_marker_pos)
			{
				return put(data+end_marker_pos+escaped_zeros, data_size-end_marker_pos-escaped_zeros);
			}
		}		

		return true;
	}
	catch (CryptoPP::Exception& e)
	{
		Server->Log(std::string("Exception during decryption (put): ")+e.what(), LL_ERROR);
		return false;
	}
}

std::string AESGCMDecryption::get( bool& has_error )
{
	try
	{
		std::string ret;
		if(decryption_filter.NumberOfMessages()>0)
		{
			ret.resize(decryption_filter.MaxRetrievable());

			if(!ret.empty())
			{
				size_t nb = decryption_filter.Get(reinterpret_cast<byte*>(&ret[0]), ret.size());
				if(nb!=ret.size())
				{
					assert(false);
					ret.resize(nb);
				}
			}

			decryption_filter.GetNextMessage();
		}

		has_error=false;
		return ret;
	}
	catch (CryptoPP::Exception& e)
	{
		Server->Log(std::string("Exception during decryption (get): ")+e.what(), LL_ERROR);
		has_error=true;
	}

	return std::string();
}

bool AESGCMDecryption::get( char *data, size_t& data_size )
{
	try
	{
		if(decryption_filter.NumberOfMessages()>0)
		{
			data_size = decryption_filter.Get(reinterpret_cast<byte*>(data), data_size);

			decryption_filter.GetNextMessage();
		}
		else
		{
			data_size=0;
		}

		return true;
	}
	catch (CryptoPP::Exception& e)
	{
		Server->Log(std::string("Exception during decryption (get2): ")+e.what(), LL_ERROR);
	}

	return false;
}

size_t AESGCMDecryption::findAndUnescapeEndMarker( const char* data, size_t data_size, std::string& data_copy,
	bool& has_copy, bool& has_error, size_t& escaped_zeros)
{
	for(size_t i=0;i<data_size;)
	{
		char ch=data[i];

		if(end_marker_state==0 && i+end_marker_zeros<=data_size
			&& data[i+end_marker_zeros-1]!=0)
		{
			i+=end_marker_zeros;
			continue;
		}


		if(end_marker_state==end_marker_zeros)
		{
			if(ch==2)
			{
				end_marker_state=0;

				if(data_copy.empty())
				{
					data_copy.insert(data_copy.begin(), data, data+data_size);
				}
				data_copy.erase(data_copy.begin()+i-escaped_zeros);
				Server->Log("Unescaped something at "+convert(i));
				++escaped_zeros;
				++overhead_bytes;
				has_copy=true;
			}
			else if(ch==1)
			{
				end_marker_state=0;

				if(i+1>escaped_zeros)
				{
					overhead_bytes+=end_marker_zeros+1;
					return i+1-escaped_zeros;
				}
				else
				{
					has_error=true;
					return std::string::npos;
				}
			}
			else if(ch!=0)
			{
				has_error=true;
				return std::string::npos;
			}
		}
		else if(ch==0)
		{
			++end_marker_state;
		}
		else
		{
			end_marker_state=0;
		}

		++i;
	}

	return std::string::npos;
}

int64 AESGCMDecryption::getOverheadBytes()
{
	return overhead_bytes;
}

bool AESGCMDecryption::hasData()
{
	return decryption_filter.NumberOfMessages()>0;
}

