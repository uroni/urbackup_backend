#include "ECIES.h"
#include "../Interface/Server.h"

ECIESEncryption::ECIESEncryption(const std::string& pubkey)
	: has_error(false)
{
	try
	{
		CryptoPP::AutoSeededRandomPool prng;
		encryptor.AccessPublicKey().Load(CryptoPP::StringSource(reinterpret_cast<const CryptoPP::byte*>(pubkey.c_str()), pubkey.size(), true).Ref());
	}
	catch (const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in ECIESEncryption::ECIESEncryption: " + e.GetWhat(), LL_ERROR);
		has_error = true;
	}
}

std::string ECIESEncryption::encrypt(const std::string& msg)
{
	try
	{
		std::string ret;
		CryptoPP::AutoSeededRandomPool prng;
		CryptoPP::StringSource ss(msg, true, new CryptoPP::PK_EncryptorFilter(prng, encryptor, new CryptoPP::StringSink(ret)));
		return ret;
	}
	catch (const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in ECIESEncryption::encrypt: " + e.GetWhat(), LL_ERROR);
		has_error = true;
		return std::string();
	}
}

ECIESDecryption::ECIESDecryption()
	: has_error(false)
{
	try
	{
		CryptoPP::AutoSeededRandomPool prng;
		decryptor.AccessKey().Initialize(prng, CryptoPP::ASN1::secp256r1());
	}
	catch (const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in ECIESDecryption::ECIESDecryption: " + e.GetWhat(), LL_ERROR);
		has_error = true;
	}
}

std::string ECIESDecryption::decrypt(const std::string& msg)
{
	try
	{
		std::string ret;
		CryptoPP::AutoSeededRandomPool prng;
		CryptoPP::StringSource ss(msg, true, new CryptoPP::PK_DecryptorFilter(prng, decryptor, new CryptoPP::StringSink(ret)));
		return ret;
	}
	catch (const CryptoPP::Exception& e)
	{
		Server->Log("Exception occured in ECIESDecryption::decrypt: " + e.GetWhat(), LL_ERROR);
		has_error = true;
		return std::string();
	}
}

std::string ECIESDecryption::getPublicKey()
{
	std::string ret;
	CryptoPP::StringSink rsink(ret);
	CryptoPP::ECIES<CryptoPP::ECP, CryptoPP::SHA256>::Encryptor encryptor(decryptor);
	encryptor.GetPublicKey().Save(rsink);
	return ret;
}
