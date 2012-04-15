#include "IZlibDecompression.h"

#ifdef _WIN32
#include <zlib.h>
#else
#include <crypto++/zlib.h>
#endif

class ZlibDecompression : public IZlibDecompression
{
public:
	virtual size_t decompress(const char *input, size_t input_size, std::vector<char> *output, bool flush, size_t output_off=0);

private:
	CryptoPP::ZlibDecompressor decomp;
};