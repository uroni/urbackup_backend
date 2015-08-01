#include "AESGCMDecryption.h"
#include "../Interface/Server.h"

const size_t iv_size = 64;

AESGCMDecryption::AESGCMDecryption( const std::string &password, bool hash_password )
	: decryption(), decryption_filter(decryption), iv_done(false)
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
			decryption.SetKeyWithIV(m_sbbKey.BytePtr(), m_sbbKey.size(),
				reinterpret_cast<const byte*>(iv_buffer.data()), iv_buffer.size());
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
		decryption_filter.Put(reinterpret_cast<const byte*>(data), data_size);
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
		ret.resize(decryption_filter.MaxRetrievable());

		if(!ret.empty())
		{
			size_t nb = decryption_filter.Get(reinterpret_cast<byte*>(&ret[0]), ret.size());
			if(nb!=ret.size())
			{
				ret.resize(nb);
			}
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
		data_size = decryption_filter.Get(reinterpret_cast<byte*>(data), data_size);
		return true;
	}
	catch (CryptoPP::Exception& e)
	{
		Server->Log(std::string("Exception during decryption (get2): ")+e.what(), LL_ERROR);
	}

	return false;
}

