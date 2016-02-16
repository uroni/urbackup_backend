#pragma once

#include "../Interface/File.h"
#include <string>
#include "ExtentIterator.h"
#include "TreeHash.h"

class INotEnoughSpaceCallback
{
public:
	virtual bool handle_not_enough_space(const std::string &path)=0;
};

void init_chunk_hasher();

std::string get_sparse_extent_content();

bool build_chunk_hashs(IFile *f, IFile *hashoutput, INotEnoughSpaceCallback *cb,
	IFsFile *copy, bool modify_inplace, int64* inplace_written=NULL,
	IFile* hashinput=NULL, bool show_pc=false, IHashFunc* hashf=NULL, IExtentIterator* extent_iterator=NULL);

bool writeRepeatFreeSpace(IFile *f, const char *buf, size_t bsize, INotEnoughSpaceCallback *cb);

bool writeFileRepeatTries(IFile *f, const char *buf, size_t bsize);
