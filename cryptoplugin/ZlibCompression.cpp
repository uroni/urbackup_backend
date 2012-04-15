#include "ZlibCompression.h"


ZlibCompression::ZlibCompression(int compression_level)
{
	comp.SetDeflateLevel(compression_level);
}

ZlibCompression::~ZlibCompression(void)
{
}

size_t ZlibCompression::compress(const char *input, size_t input_length, std::vector<char> *output, bool flush, size_t output_off)
{
	comp.Put((const byte*)input, input_length);
	if(flush)
	{
		comp.Flush(true);
	}
	size_t rc=(size_t)comp.MaxRetrievable();

	if(output->size()<rc+output_off)
	{
		output->resize(rc+output_off);
	}

	return comp.Get((byte*)(&(*output)[output_off]), rc);
}