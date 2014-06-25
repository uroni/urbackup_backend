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

#include "../vld.h"
#include "CryptoFactory.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

#include "AESEncryption.h"
#include "AESDecryption.h"
#include "ZlibCompression.h"
#include "ZlibDecompression.h"

#include "cryptopp_inc.h"

IAESEncryption* CryptoFactory::createAESEncryption(const std::string &password)
{
	return new AESEncryption(password, true);
}

IAESDecryption* CryptoFactory::createAESDecryption(const std::string &password)
{
	return new AESDecryption(password, true);
}

IAESEncryption* CryptoFactory::createAESEncryptionNoDerivation(const std::string &password)
{
	return new AESEncryption(password, false);
}

IAESDecryption* CryptoFactory::createAESDecryptionNoDerivation(const std::string &password)
{
	return new AESDecryption(password, false);
}

bool CryptoFactory::generatePrivatePublicKeyPair(const std::string &keybasename)
{
	CryptoPP::AutoSeededRandomPool rnd;

	CryptoPP::DSA::PrivateKey dsaPrivate;
	dsaPrivate.GenerateRandomWithKeySize(rnd, 1024);

	Server->Log("Calculating public key...", LL_INFO);
	CryptoPP::DSA::PublicKey dsaPublic;
	dsaPublic.AssignFrom(dsaPrivate);

	if (!dsaPrivate.Validate(rnd, 3) || !dsaPublic.Validate(rnd, 3))
	{
		Server->Log("Validating key pair failed", LL_ERROR);
		return false;
	}

	dsaPrivate.Save(CryptoPP::FileSink((keybasename+".priv").c_str()).Ref());
	dsaPublic.Save(CryptoPP::FileSink((keybasename+".pub").c_str()).Ref());
	return true;
}

bool CryptoFactory::signFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)
{
	CryptoPP::DSA::PrivateKey PrivateKey; 
	CryptoPP::AutoSeededRandomPool rnd;

	try
	{
		PrivateKey.Load(CryptoPP::FileSource(keyfilename.c_str(), true).Ref());
		
		CryptoPP::DSA::Signer signer( PrivateKey );
		CryptoPP::FileSource( filename.c_str(), true, 
				new CryptoPP::SignerFilter( rnd, signer,
				new CryptoPP::FileSink( sigfilename.c_str() )
				) // SignerFilter
			);

		return true;
	}
	catch(const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in CryptoFactory::signFile: " + e.GetWhat(), LL_ERROR);
	}

	return false;
}

bool CryptoFactory::verifyFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)
{
	CryptoPP::DSA::PublicKey PublicKey; 
	CryptoPP::AutoSeededRandomPool rnd;

	try
	{
		PublicKey.Load(CryptoPP::FileSource(keyfilename.c_str(), true).Ref());
		
		CryptoPP::DSA::Verifier verifier( PublicKey );
		CryptoPP::SignatureVerificationFilter svf(verifier);

		CryptoPP::FileSource( sigfilename.c_str(), true, new CryptoPP::Redirector( svf, CryptoPP::Redirector::PASS_WAIT_OBJECTS ) );
		CryptoPP::FileSource( filename.c_str(), true, new CryptoPP::Redirector( svf ) );

		return svf.GetLastResult();
	}
	catch(const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in CryptoFactory::verifyFile: " + e.GetWhat(), LL_ERROR);
	}

	return false;
}

std::string CryptoFactory::generatePasswordHash(const std::string &password, const std::string &salt, size_t iterations)
{
	CryptoPP::SecByteBlock derived;
	derived.resize(CryptoPP::SHA512::DIGESTSIZE);
	CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pkcs;
	pkcs.DeriveKey(derived, CryptoPP::SHA512::DIGESTSIZE, 0, (byte*)password.c_str(), password.size(), (byte*)salt.c_str(), salt.size(), (unsigned int)iterations, 0);

	CryptoPP::HexEncoder hexEncoder;
	hexEncoder.Put(derived,derived.size());
	hexEncoder.MessageEnd();
	std::string ret;
	ret.resize(hexEncoder.MaxRetrievable());
	hexEncoder.Get((byte*)&ret[0],ret.size());
	return ret;
}

std::string CryptoFactory::generateBinaryPasswordHash(const std::string &password, const std::string &salt, size_t iterations)
{
	std::string derived;
	derived.resize(CryptoPP::SHA512::DIGESTSIZE);
	CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pkcs;
	pkcs.DeriveKey((byte*)derived.data(), CryptoPP::SHA512::DIGESTSIZE, 0, (byte*)password.c_str(), password.size(), (byte*)salt.c_str(), salt.size(), (unsigned int)iterations, 0);

	return derived;
}

IZlibCompression* CryptoFactory::createZlibCompression(int compression_level)
{
	return new ZlibCompression(compression_level);
}

IZlibDecompression* CryptoFactory::createZlibDecompression(void)
{
	return new ZlibDecompression;
}

bool CryptoFactory::signData(const std::string &pubkey, const std::string &data, std::string &signature)
{
	CryptoPP::DSA::PrivateKey PrivateKey; 
	CryptoPP::AutoSeededRandomPool rnd;

	try
	{
		PrivateKey.Load(CryptoPP::StringSource(reinterpret_cast<const byte*>(pubkey.c_str()), pubkey.size(), true).Ref());

		CryptoPP::DSA::Signer signer( PrivateKey );
		CryptoPP::StringSource( reinterpret_cast<const byte*>(data.c_str()), data.size(), true,
			new CryptoPP::SignerFilter( rnd, signer,
			new CryptoPP::StringSink( signature )
			) // SignerFilter
			);

		return true;
	}
	catch(const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in CryptoFactory::signData: " + e.GetWhat(), LL_ERROR);
	}

	return false;
}

bool CryptoFactory::verifyData( const std::string &pubkey, const std::string &data, const std::string &signature )
{
	CryptoPP::DSA::PublicKey PublicKey; 
	CryptoPP::AutoSeededRandomPool rnd;

	try
	{
		PublicKey.Load(CryptoPP::StringSource(reinterpret_cast<const byte*>(pubkey.c_str()), pubkey.size(), true).Ref());

		CryptoPP::DSA::Verifier verifier( PublicKey );
		CryptoPP::SignatureVerificationFilter svf(verifier);

		CryptoPP::StringSource( reinterpret_cast<const byte*>(signature.c_str()), signature.size(), true, new CryptoPP::Redirector( svf, CryptoPP::Redirector::PASS_WAIT_OBJECTS ) );
		CryptoPP::StringSource( reinterpret_cast<const byte*>(data.c_str()), data.size(), true, new CryptoPP::Redirector( svf ) );

		return svf.GetLastResult();
	}
	catch(const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in CryptoFactory::verifyData: " + e.GetWhat(), LL_ERROR);
	}

	return false;
}

std::string CryptoFactory::encryptAuthenticatedAES(const std::string& data, const std::string &password, size_t iterations/*=20000*/ )
{
	const size_t iv_size=CryptoPP::AES::BLOCKSIZE;
	std::string ciphertext;
	ciphertext.resize(iv_size);

	Server->secureRandomFill(&ciphertext[0], iv_size);

	CryptoPP::SecByteBlock key;
	key.resize(CryptoPP::SHA256::DIGESTSIZE);

	CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> gen;
	gen.DeriveKey(key.BytePtr(), CryptoPP::SHA256::DIGESTSIZE, 0, reinterpret_cast<const byte*>(password.c_str()), password.size(),
		reinterpret_cast<const byte*>(ciphertext.data()), iv_size, static_cast<unsigned int>(iterations), 0);

	CryptoPP::EAX<CryptoPP::AES>::Encryption enc;
	enc.SetKeyWithIV(key.BytePtr(), CryptoPP::AES::BLOCKSIZE, reinterpret_cast<const byte*>(ciphertext.data()), iv_size);

	
	CryptoPP::ArraySource( reinterpret_cast<const byte*>(data.data()), data.size(), true,
		new CryptoPP::AuthenticatedEncryptionFilter( enc,
		new CryptoPP::StringSink( ciphertext )
		) );

	return ciphertext;
}

std::string CryptoFactory::decryptAuthenticatedAES(const std::string& data, const std::string &password, size_t iterations)
{
	const size_t iv_size=CryptoPP::AES::BLOCKSIZE;
	if(data.size()<iv_size)
	{
		return std::string();
	}

	CryptoPP::SecByteBlock key;
	key.resize(CryptoPP::SHA256::DIGESTSIZE);

	CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> gen;
	gen.DeriveKey(key.BytePtr(), CryptoPP::SHA256::DIGESTSIZE, 0, reinterpret_cast<const byte*>(password.c_str()), password.size(),
		reinterpret_cast<const byte*>(data.data()), iv_size, static_cast<unsigned int>(iterations), 0);

	CryptoPP::EAX<CryptoPP::AES>::Decryption dec;
	dec.SetKeyWithIV(key.BytePtr(), CryptoPP::AES::BLOCKSIZE, reinterpret_cast<const byte*>(data.data()), iv_size);
		
	
	try
	{
		std::string ret;
		CryptoPP::ArraySource( reinterpret_cast<const byte*>(data.data()+iv_size), data.size()-iv_size, true,
			new CryptoPP::AuthenticatedDecryptionFilter( dec,
				new CryptoPP::StringSink( ret ) ) );
		return ret;
	}	
	catch(const CryptoPP::HashVerificationFilter::HashVerificationFailed&)
	{
		return std::string();
	}
}
