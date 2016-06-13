#pragma once
#include "../Interface/Object.h"


class IECDHKeyExchange : public IObject
{
public:
	virtual std::string getPublicKey() = 0;
	virtual std::string getSharedKey(const std::string& other_public) = 0;
};