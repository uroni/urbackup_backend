#ifndef IZLIBCOMPRESSION_H
#define IZLIBCOMPRESSION_H

#include <vector>

#include "../Interface/Object.h"

class IZlibCompression : public IObject
{
public:
	virtual size_t compress(const char *input, size_t input_length, std::vector<char> *output, bool flush, size_t output_off=0)=0;
};

#endif //IZLIBCOMPRESSION_H