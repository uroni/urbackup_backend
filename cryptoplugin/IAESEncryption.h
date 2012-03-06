#ifndef IAESENCRYPTION_H
#define IAESENCRYPTION_H

#include <string>

#include "../Interface/Object.h"

class IAESEncryption : public IObject
{
public:
	virtual std::string encrypt(const std::string &data)=0;
	virtual std::string encrypt(const char *data, size_t data_size)=0;
	virtual std::string encrypt(char *data, size_t data_size)=0;
};

#endif