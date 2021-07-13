#pragma once
#include "../Interface/File.h"
#include "../Interface/Object.h"
#include <string>
#include "IOnlineKvStore.h"

class ICompressAndEncrypt : public IObject
{
public:
	virtual size_t read(char* buffer, size_t buffer_size) = 0;
	virtual int64 get_generation() = 0;
	virtual std::string md5sum() = 0;
	virtual size_t readBytes() = 0;
};

class IDecryptAndDecompress : public IObject
{
public:
	virtual bool put(char* buffer, size_t buffer_size) = 0;

	virtual bool finalize() = 0;

	virtual std::string md5sum() = 0;
};

class IOnlineKvStore;

namespace
{
	const unsigned int CompressionLzma5 = 0;
	const unsigned int CompressionZlib5 = 1;
	const unsigned int CompressionZstd3 = 2;
	const unsigned int CompressionZstd19 = 3;
	const unsigned int CompressionZstd9 = 4;
	const unsigned int CompressionNone = 5;
	const unsigned int CompressionZstd7 = 6;
	const unsigned int CompressionLzo = 7;
}

unsigned int CompressionMethodFromString(const std::string& str);
std::string CompressionMethodToBtrfsString(unsigned int cmeth);

class ICompressEncryptFactory : public IObject
{
public:
	virtual ICompressAndEncrypt* createCompressAndEncrypt(const std::string& encryption_key, IFile* file, IOnlineKvStore* online_kv_store, unsigned int compression_id) = 0;
	virtual IDecryptAndDecompress* createDecryptAndDecompress(const std::string& encryption_key, IFile* output_file) = 0;
};

ICompressEncryptFactory* get_compress_encrypt_factory();