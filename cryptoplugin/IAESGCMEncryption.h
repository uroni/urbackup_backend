#pragma once

#include "../Interface/Object.h"

class IAESGCMEncryption : public IObject
{
public:
	virtual void put(const char *data, size_t data_size) = 0;
	virtual void flush() = 0;
	virtual std::string get() = 0;

	virtual int64 getOverheadBytes() = 0;
};