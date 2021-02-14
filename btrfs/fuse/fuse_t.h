#pragma once
#include "../../Interface/Types.h"

struct SBtrfsFile
{
	SBtrfsFile() :
		size(0), last_modified(0),
		created(0), accessed(0),
		isdir(false), issym(false),
		isspecialf(false)
	{

	}

	std::string name;
	int64 size;
	int64 last_modified;
	int64 created;
	int64 accessed;
	bool isdir;
	bool issym;
	bool isspecialf;

	bool operator<(const SBtrfsFile& other) const
	{
		return name < other.name;
	}
};