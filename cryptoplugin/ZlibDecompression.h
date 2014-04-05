#include "IZlibDecompression.h"

#include "cryptopp_inc.h"

class ZlibDecompression : public IZlibDecompression
{
public:
	virtual size_t decompress(const char *input, size_t input_size, std::vector<char> *output, bool flush, size_t output_off=0, bool *error=NULL);

	virtual size_t decompress(const char *input, size_t input_size, char* output, size_t output_size, bool flush, bool *error=NULL);

private:
	CryptoPP::ZlibDecompressor decomp;
};