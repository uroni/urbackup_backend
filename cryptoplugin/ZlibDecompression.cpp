#include "ZlibDecompression.h"


size_t ZlibDecompression::decompress(const char *input, size_t input_size, std::vector<char> *output, bool flush, size_t output_off)
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