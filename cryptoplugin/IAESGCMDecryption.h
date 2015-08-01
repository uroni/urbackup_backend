#pragma once

#include <string>

#include "../Interface/Object.h"

class IAESGCMDecryption : public IObject
{
public:
	virtual bool put(const char *data, size_t data_size) = 0;
	virtual std::string get(bool& has_error) = 0;
	virtual bool get(char *data, size_t& data_size) = 0;
};