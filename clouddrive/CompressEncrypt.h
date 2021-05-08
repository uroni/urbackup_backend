#pragma once

#include "ICompressEncrypt.h"
#include "../cryptoplugin/cryptopp_inc.h"
#include "../md5.h"

class CompressEncryptFactory : public ICompressEncryptFactory
{
public:
	ICompressAndEncrypt* createCompressAndEncrypt(const std::string& encryption_key, IFile* file, IOnlineKvStore* online_kv_store, unsigned int compression_id);
	IDecryptAndDecompress* createDecryptAndDecompress(const std::string& encryption_key, IFile* output_file);
};

void init_compress_encrypt_factory();

namespace
{
	enum CompressResult
	{
		CompressResult_Ok,
		CompressResult_End,
		CompressResult_Other
	};
	
	enum DecompressResult
	{
		DecompressResult_Ok,
		DecompressResult_End,
		DecompressResult_Other
	};
}

class ICompressor : public IObject
{
public:
	virtual void setOut(char* next_out, size_t avail_out) = 0 ;
	virtual void setIn(char* next_in, size_t avail_in) = 0;
	virtual size_t getAvailOut() = 0;
	virtual size_t getAvailIn() = 0;
	virtual CompressResult compress(bool finish, int& code) = 0;
	virtual unsigned int getId() = 0;
};

class IDecompressor : public IObject
{
public:
	virtual void setOut(char* next_out, size_t avail_out) = 0;
	virtual void setIn(char* next_in, size_t avail_in) = 0;
	virtual size_t getAvailOut() = 0;
	virtual size_t getAvailIn() = 0;
	virtual DecompressResult decompress(int& code) = 0;
};


class CompressAndEncrypt : public ICompressAndEncrypt
{
public:
	CompressAndEncrypt(const std::string& encryption_key, IFile* file, IOnlineKvStore* online_kv_store, ICompressor* compressor);

	size_t read(char* buffer, size_t buffer_size);

	int64 get_generation();

	std::string md5sum();

	size_t readBytes()
	{
		return ret_bytes;
	}

private:
	std::vector<char> output_buffer;
	size_t output_buffer_pos;

	IFile* file;
	int64 file_pos;
	IOnlineKvStore* online_kv_store;

	CryptoPP::GCM< CryptoPP::AES >::Encryption encryption;
	CryptoPP::AuthenticatedEncryptionFilter encryption_filter;

	std::unique_ptr<ICompressor> compressor;
	std::vector<char> read_buffer;
	std::vector<char> compressed_buffer;

	bool compression_ended;

	int64 generation;

	size_t ret_bytes;
	int64 input_file_size;

	MD5 md5;
};

const size_t iv_size_v1 = 24;
const size_t iv_size_v2 = 12;

enum EReadState
{
	EReadState_Version,
	EReadState_Iv,
	EReadState_Data
};

class DecryptAndDecompress : public IDecryptAndDecompress
{
public:
	DecryptAndDecompress(const std::string& encryption_key, IFile* output_file);

	bool put(char* buffer, size_t buffer_size);

	bool finalize();

	std::string md5sum();

private:

	bool decrypt();

	bool init_decompression(unsigned int decompressor_id);

	unsigned int version;
	size_t iv_size;
	EReadState read_state;
	char header_buf[30];
	size_t header_buf_pos;
	std::unique_ptr<IDecompressor> decompressor;
	std::vector<char> decrypted_buffer;
	std::vector<char> output_buf;

	CryptoPP::GCM< CryptoPP::AES >::Decryption decryption;
	CryptoPP::AuthenticatedDecryptionFilter decryption_filter;

	std::string encryption_key;
	IFile* output_file;
	int64 file_pos;

	MD5 md5;
};

bool read_generation( IFile* file, int64 offset, int64& generation);