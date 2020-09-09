#pragma once

#include <string>

class IECIESEncryption
{
public:
	virtual std::string encrypt(const std::string& msg) = 0;
};

class IECIESDecryption
{
public:
	virtual std::string decrypt(const std::string& msg) = 0;
	virtual std::string getPublicKey() = 0;
};