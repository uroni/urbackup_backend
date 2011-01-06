#ifndef IAESDECRYPTION_H
#define IAESDECRYPTION_H

#include <string>

#include "../Interface/Object.h"

class IAESDecryption : public IObject
{
public:
	virtual std::string decrypt(const std::string &data)=0;
};

#endif