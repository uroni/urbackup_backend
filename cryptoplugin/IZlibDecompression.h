#ifndef IZLIBDECOMPRESSION_H
#define IZLIBDECOMPRESSION_H

#include <vector>
#include <string>

#include "../Interface/Object.h"

class IZlibDecompression : public IObject
{
public:
	virtual size_t decompress(const char *input, size_t input_size, std::vector<char> *output, bool flush, size_t output_off=0, bool *error=NULL)=0;
};

#endif //IZLIBDECOMPRESSION_H