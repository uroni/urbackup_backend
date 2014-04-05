#include "ZlibDecompression.h"
#include "../Interface/Server.h"


size_t ZlibDecompression::decompress(const char *input, size_t input_size, std::vector<char> *output, bool flush, size_t output_off, bool *error)
{
	try
	{
		decomp.Put((const byte*)input, input_size);
		if(flush)
		{
			decomp.Flush(true);
		}
		size_t rc=(size_t)decomp.MaxRetrievable();
		if(rc>0)
		{
			if(output->size()<rc+output_off)
			{
				output->resize(rc+output_off);
			}
			return decomp.Get((byte*)(&(*output)[output_off]), rc);
		}
		else
		{
			return 0;
		}
	}
	catch(const CryptoPP::ZlibDecompressor::Err& err)
	{
		Server->Log("Error during ZLib decompression: "+err.GetWhat(), LL_WARNING);
		if(error!=NULL)
		{
			*error=true;
		}
		return 0;
	}
}

size_t ZlibDecompression::decompress( const char *input, size_t input_size, char* output, size_t output_size, bool flush, bool *error)
{
	try
	{
		decomp.Put((const byte*)input, input_size);
		if(flush)
		{
			decomp.Flush(true);
		}
		return decomp.Get(reinterpret_cast<byte*>(output), output_size);
	}
	catch(const CryptoPP::ZlibDecompressor::Err& err)
	{
		Server->Log("Error during ZLib decompression: "+err.GetWhat(), LL_WARNING);
		if(error!=NULL)
		{
			*error=true;
		}
		return 0;
	}
}
