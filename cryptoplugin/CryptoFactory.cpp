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
#include "CryptoFactory.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

#include "AESEncryption.h"
#include "AESDecryption.h"

#ifdef _WIN32
#include <dsa.h>
#include <osrng.h>
#include <files.h>
#else
#include <crypto++/dsa.h>
#include <crypto++/osrng.h>
#include <crypto++/files.h>
#endif

IAESEncryption* CryptoFactory::createAESEncryption(const std::string &password)
{
	return new AESEncryption(password);
}

IAESDecryption* CryptoFactory::createAESDecryption(const std::string &password)
{
	return new AESDecryption(password);
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
	catch(...)
	{
		Server->Log("Exception occured in CryptoFactory::signFile", LL_ERROR);
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
	catch(...)
	{
		Server->Log("Exception occured in CryptoFactory::verifyFile", LL_ERROR);
	}

	return false;
}