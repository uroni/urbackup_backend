#pragma once
#include "../Interface/Pipe.h"
#include <string>

class IFileMetadataPipe
{
public:
	virtual IPipe* getErrPipe() = 0;

	virtual bool openOsMetadataFile(const std::string& fn) = 0;

	virtual bool readCurrOsMetadata(char* buf, size_t buf_avail, size_t& read_bytes) = 0;
};