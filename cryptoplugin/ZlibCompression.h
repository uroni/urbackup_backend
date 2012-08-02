#include "IZlibCompression.h"

#ifdef _WIN32
#include <zlib.h>
#else
#include <crypto++/zlib.h>
#endif

class ZlibCompression : public IZlibCompression
{
public:
	ZlibCompression(int compression_level);
	~ZlibCompression(void);
	virtual size_t compress(const char *input, size_t input_length, std::vector<char> *output, bool flush, size_t output_off=0);

private:
	CryptoPP::ZlibCompressor comp;
};