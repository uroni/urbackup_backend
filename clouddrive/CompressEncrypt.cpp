/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/
#include "CompressEncrypt.h"
#include "../stringtools.h"
#include <stdexcept>
#include "CdZlibCompressor.h"
#include "LzmaCompressor.h"
#include "CdZstdCompressor.h"
#include "../urbackupcommon/os_functions.h"
#include "../urbackupcommon/events.h"

using namespace CryptoPPCompat;

namespace
{
	ICompressEncryptFactory* compress_encrypt_factory;
}

void init_compress_encrypt_factory()
{
	compress_encrypt_factory = new CompressEncryptFactory;
}

unsigned int CompressionMethodFromString(const std::string & str)
{
	if (str == "lzma_5")
		return CompressionLzma5;
	else if (str == "zlib_5")
		return CompressionZlib5;
	else if (str == "zstd_3")
		return CompressionZstd3;
	else if (str == "zstd_19")
		return CompressionZstd19;
	else if (str == "zstd_9")
		return CompressionZstd9;
	else if (str == "none")
		return CompressionNone;
	else if (str == "zstd_7")
		return CompressionZstd7;
	else if (str == "lzo" )
		return CompressionLzo;
	else
		return CompressionZstd3;
}

std::string CompressionMethodToBtrfsString(unsigned int cmeth)
{
	switch (cmeth)
	{
	case CompressionZlib5:
		return "zlib:5";
	case CompressionZstd3:
		return "zstd:3";
	case CompressionZstd19:
		return "zstd:15";
	case CompressionZstd9:
		return "zstd:9";
	case CompressionZstd7:
		return "zstd:7";
	case CompressionNone:
		return "none";
	case CompressionLzo:
		return "lzo";
	default:
		return "zstd:3";
	}
}

ICompressEncryptFactory* get_compress_encrypt_factory()
{
	return compress_encrypt_factory;
}

bool read_generation( IFile* file, int64 offset, int64& generation)
{
	char header[sizeof(unsigned int)+12];

	if(file->Read(offset, header, sizeof(header))!=sizeof(header))
	{
		Server->Log("Error reading file header for generation. "+os_last_error_str(), LL_ERROR);
		return false;
	}

	unsigned int version;
	memcpy(&version, header, sizeof(version));
	version = little_endian(version);

	unsigned int version_part = version & 0x0000FFFF;

	if(version_part !=2)
	{
		Server->Log("Unknown object version: "+convert(version_part)+" while reading file header for generation", LL_ERROR);
		return false;
	}

	generation = 0;
	memcpy(&generation, header+sizeof(version)+6, 6);
	generation = little_endian(generation);

	return true;
}

ICompressAndEncrypt* CompressEncryptFactory::createCompressAndEncrypt(const std::string& encryption_key, IFile* file, IOnlineKvStore* online_kv_store, unsigned int compression_id)
{
	ICompressor* compressor;
	switch (compression_id)
	{
	case CompressionLzma5:
#ifdef WITH_LZMA
		compressor = new LzmaCompressor;
#else
		compressor = new CdZstdCompressor(17, CompressionZstd3);
#endif
		break;

	case CompressionZlib5:
		compressor = new CdZlibCompressor(5, compression_id);
		break;
	case CompressionZstd3:
	case CompressionZstd19:
	case CompressionZstd9:
	case CompressionZstd7:
	{
		int level = 3;
		switch (compression_id)
		{
		case CompressionZstd19:
			level = 19;
		case CompressionZstd9:
			level = 9;
		case CompressionZstd7:
			level = 7;
		}
		compressor = new CdZstdCompressor(level, CompressionZstd3);
	} break;
	case CompressionNone:
		compressor = nullptr;
		break;
	default: 
		return nullptr;
	}
    return new CompressAndEncrypt(encryption_key, file, online_kv_store, compressor);
}

IDecryptAndDecompress* CompressEncryptFactory::createDecryptAndDecompress(const std::string& encryption_key, IFile* output_file)
{
    return new DecryptAndDecompress(encryption_key, output_file);
}



CompressAndEncrypt::CompressAndEncrypt( const std::string& encryption_key, IFile* file, IOnlineKvStore* online_kv_store, ICompressor* compressor) 
	: file(file), online_kv_store(online_kv_store), encryption(), encryption_filter(encryption),
	compression_ended(false), ret_bytes(0), compressor(compressor), file_pos(0),
	input_file_size(file->Size())
{
	char iv[12];
	CryptoPP::AutoSeededRandomPool prng;
	prng.GenerateBlock(reinterpret_cast<byte*>(iv), 6);

	generation = online_kv_store->generation_inc(1);

	uint64 ugen = static_cast<uint64>(generation);

	if( ugen & 0xFFFF000000000000ULL)
	{
		Server->Log("Generation overflow. There is a small probability of nonce reuse.", LL_INFO);

		ugen = ugen ^ ( (ugen >> 16) & 0xFFFF00000000 );
	}

	ugen = little_endian(ugen);

	memcpy(&iv[6], &ugen, 6);			

	encryption.SetKeyWithIV(reinterpret_cast<const byte*>(encryption_key.data()), encryption_key.size(),
		reinterpret_cast<const byte*>(iv), sizeof(iv));

	unsigned int version=2;
	unsigned int compression_id;
	if (!compressor)
		compression_id = CompressionNone;
	else
		compression_id = compressor->getId();

	version = version | (compression_id << 16);

	output_buffer.resize(sizeof(version) + sizeof(iv));

	memcpy(&output_buffer[0], &version, sizeof(version));
	memcpy(&output_buffer[sizeof(version)], iv, sizeof(iv));

	output_buffer_pos = 0;

	const size_t read_buffer_size = 128 * 1024;
	read_buffer.resize(read_buffer_size);

	if (compressor)
	{
		const size_t enc_buffer_size = 128 * 1024;
		compressed_buffer.resize(enc_buffer_size);

		compressor->setOut(compressed_buffer.data(), compressed_buffer.size());
	}
}

size_t CompressAndEncrypt::read( char* buffer, size_t buffer_size )
{
	size_t ret_size = 0;

	if(!output_buffer.empty())
	{
		size_t toread = (std::min)(buffer_size, output_buffer.size()-output_buffer_pos);

		memcpy(buffer, &output_buffer[output_buffer_pos], toread);
		md5.update(reinterpret_cast<unsigned char*>(buffer), static_cast<unsigned int>(toread));

		output_buffer_pos+=toread;

		buffer+=toread;
		buffer_size-=toread;
		ret_size+=toread;

		if(output_buffer_pos==output_buffer.size())
		{
			output_buffer.clear();
		}

		if (buffer_size == 0)
		{
			return ret_size;
		}
	}

	if(compression_ended)
	{
		size_t ret_add = encryption_filter.Get(reinterpret_cast<byte*>(buffer), buffer_size);
		if(ret_add>0)
		{
			md5.update(reinterpret_cast<unsigned char*>(buffer), static_cast<unsigned int>(ret_add));
		}
		ret_bytes+=ret_size + ret_add;
		return ret_size + ret_add;
	}

	try
	{	
		do
		{
			_u32 file_read;
			if( (!compressor || compressor->getAvailIn()==0 )
				&& file!=nullptr)
			{
				bool has_read_error = false;
				size_t toread = (std::min)(static_cast<size_t>(input_file_size - file_pos), buffer_size);
				if (toread > 0)
					file_read = file->Read(file_pos, read_buffer.data(),
						static_cast<_u32>(toread), &has_read_error);
				else
					file_read = 0;

				if (has_read_error)
				{
					std::string msg = "Read error while reading from file "
						+ file->getFilename() + " at position " + convert(file_pos)
						+ " len " + convert(buffer_size) + " for compression and encryption. " + os_last_error_str();
					Server->Log(msg, LL_ERROR);
					addSystemEvent("cache_err",
						"Error reading from file on cache",
						msg, LL_ERROR);
					return std::string::npos;
				}

				if(file_read >0)
				{
					if(compressor)
					{
						compressor->setIn(read_buffer.data(), file_read);
					}
					file_pos += file_read;
				}
				else
				{
					if (file_pos < input_file_size)
					{
						std::string msg = "Read only " + convert(file_pos) + " of total " +
							convert(input_file_size) + " from " + file->getFilename();
						Server->Log(msg, LL_ERROR);
						addSystemEvent("cache_err",
							"Error reading from file on cache",
							msg, LL_ERROR);
						return std::string::npos;
					}
					file=nullptr;
				}
			}

			int code;
			CompressResult ret;
			if (compressor)
				ret = compressor->compress(file == nullptr, code);
			else
				ret = file==nullptr ? CompressResult_End : CompressResult_Ok;

			if(ret == CompressResult_End || 
				compressor==nullptr || compressor->getAvailOut()==0 )
			{
				size_t write_size;
				if (!compressor)
					write_size = file_read;
				else
					write_size = compressed_buffer.size() - compressor->getAvailOut();

				if(write_size>0)
				{
					if (compressor)
						encryption_filter.Put(reinterpret_cast<const byte*>(compressed_buffer.data()), write_size);
					else
						encryption_filter.Put(reinterpret_cast<const byte*>(read_buffer.data()), write_size);

					size_t ret_add = encryption_filter.Get(reinterpret_cast<byte*>(buffer), buffer_size);
					md5.update(reinterpret_cast<unsigned char*>(buffer), static_cast<unsigned int>(ret_add));

					buffer+=ret_add;
					buffer_size-=ret_add;
					ret_size+=ret_add;
				}				

				if(compressor)
					compressor->setOut(compressed_buffer.data(), compressed_buffer.size());
			}

			if(ret != CompressResult_Ok)
			{
				if(ret == CompressResult_End)
				{
					compression_ended=true;
					encryption_filter.MessageEnd();

					if(buffer_size>0)
					{
						size_t add_size = read(buffer, buffer_size);

						if(add_size==std::string::npos)
						{
							return add_size;
						}
						else
						{
							ret_size+=add_size;
							return ret_size;
						}
					}
				}
				else
				{
					Server->Log("Error while compressing (code: "+convert(code)+")", LL_ERROR);
					return std::string::npos;
				}
			}
		}	
		while(ret_size==0);
	}
	catch(CryptoPP::Exception& e)
	{
		Server->Log(std::string("Exception during encryption: ")+e.what(), LL_ERROR);
		return std::string::npos;
	}

	ret_bytes+=ret_size;
	return ret_size;
}

int64 CompressAndEncrypt::get_generation()
{
	return generation;
}

std::string CompressAndEncrypt::md5sum()
{
	md5.finalize();
	return std::string(reinterpret_cast<char*>(md5.raw_digest_int()), 16);
}


DecryptAndDecompress::DecryptAndDecompress( const std::string& encryption_key, IFile* output_file ) : read_state(EReadState_Version), header_buf_pos(0), decryption(), decryption_filter(decryption),
	encryption_key(encryption_key), output_file(output_file), file_pos(0)
{
	const size_t buffer_size = 128 * 1024;
	decrypted_buffer.resize(buffer_size);
}

bool DecryptAndDecompress::put( char* buffer, size_t buffer_size )
{
	if(read_state==EReadState_Version)
	{
		size_t toread = (std::min)(sizeof(version)-header_buf_pos, buffer_size);

		memcpy(header_buf, buffer, toread);

		md5.update(reinterpret_cast<unsigned char*>(buffer), static_cast<unsigned int>(toread));

		header_buf_pos+=toread;

		if(header_buf_pos==sizeof(version))
		{
			header_buf_pos=0;
			read_state = EReadState_Iv;
			memcpy(&version, header_buf, sizeof(version));

			unsigned int version_part = version & 0x0000FFFF;

			if(version_part ==1)
			{
				iv_size = iv_size_v1;
			}
			else if(version_part ==2)
			{
				iv_size = iv_size_v2;
			}
			else
			{
				Server->Log("Unknown block version: "+convert(version_part), LL_ERROR);
				return false;
			}

			if (!init_decompression((version & 0xFFFF0000) >> 16))
			{
				Server->Log("Error during decompression init. Decompressor id " + convert((version & 0xFFFF0000) >> 16), LL_ERROR);
				return false;
			}

			if (decompressor)
			{
				const size_t buffer_size = 128 * 1024;
				output_buf.resize(buffer_size);
				decompressor->setOut(output_buf.data(), output_buf.size());
			}

			if(buffer_size>toread)
			{
				return put(buffer+toread, buffer_size-toread);
			}
		}
	}
	else if(read_state == EReadState_Iv)
	{
		size_t toread = (std::min)(iv_size-header_buf_pos, buffer_size);

		memcpy(header_buf, buffer, toread);

		md5.update(reinterpret_cast<unsigned char*>(buffer), static_cast<unsigned int>(toread));

		header_buf_pos+=toread;

		if(header_buf_pos==iv_size)
		{
			read_state = EReadState_Data;

			decryption.SetKeyWithIV(reinterpret_cast<const byte*>(encryption_key.data()), encryption_key.size(),
				reinterpret_cast<const byte*>(header_buf), iv_size);

			if(buffer_size>toread)
			{
				return put(buffer+toread, buffer_size-toread);
			}
		}

		return true;
	}

	md5.update(reinterpret_cast<unsigned char*>(buffer), static_cast<unsigned int>(buffer_size));

	try
	{
		decryption_filter.Put(reinterpret_cast<byte*>(buffer), buffer_size);

		return decrypt();
	}
	catch(CryptoPP::Exception& e)
	{
		Server->Log(std::string("Exception during decryption: ")+e.what(), LL_ERROR);
		return false;
	}
}

bool DecryptAndDecompress::finalize()
{
	try
	{
		decryption_filter.MessageEnd();

		while(decryption_filter.AnyRetrievable())
		{
			if(!decrypt())
			{
				return false;
			}
		}
		return true;
	}
	catch(CryptoPP::Exception& e)
	{
		Server->Log(std::string("Exception during decryption (finalize): ")+e.what(), LL_ERROR);
		return false;
	}
}

std::string DecryptAndDecompress::md5sum()
{
	md5.finalize();
	return md5.hex_digest();
}

bool DecryptAndDecompress::decrypt()
{
	size_t decrypted_size = decryption_filter.Get(reinterpret_cast<byte*>(decrypted_buffer.data()), decrypted_buffer.size());

	if (!decompressor)
	{
		if (output_file->Write(file_pos, decrypted_buffer.data(), static_cast<_u32>(decrypted_size)) != decrypted_size)
		{
			Server->Log("Error writing data to output file. " + os_last_error_str(), LL_ERROR);
			return false;
		}
		file_pos += decrypted_size;
		return true;
	}

	decompressor->setIn(decrypted_buffer.data(), decrypted_size);

	while (decompressor->getAvailIn()>0)
	{
		int code;
		DecompressResult ret = decompressor->decompress(code);

		if(decompressor->getAvailOut()==0 || ret==DecompressResult_End )
		{
			size_t write_size = output_buf.size() - decompressor->getAvailOut();
			if(write_size>0)
			{
				if(output_file->Write(file_pos, output_buf.data(), static_cast<_u32>(write_size) )!=write_size)
				{
					Server->Log("Error writing data to output file. "+os_last_error_str(), LL_ERROR);
					return false;
				}
				file_pos += write_size;
			}

			decompressor->setOut(output_buf.data(), output_buf.size());
		}

		if(ret!= DecompressResult_Ok)
		{
			if (ret == DecompressResult_End)
			{
				return true;
			}
			else
			{
				Server->Log("Error while decompressing (code: "+convert(code)+")", LL_ERROR);
				return false;
			}
		}
	}

	return true;
}

bool DecryptAndDecompress::init_decompression(unsigned int decompressor_id)
{
	switch (decompressor_id)
	{
#ifdef WITH_LZMA
	case CompressionLzma5:
		decompressor.reset(new LzmaDecompressor);
		break;
#endif
	case CompressionZlib5:
		decompressor.reset(new CdZlibDecompressor);
		break;
	case CompressionZstd3:
		decompressor.reset(new CdZstdDecompressor);
		break;
	case CompressionNone:
		decompressor.reset();
		return true;
	default:
		return false;
	}

	return true;
}
