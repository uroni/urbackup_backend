#include "Interface/OutputStream.h"

class CStringOutputStream : public IOutputStream
{
public:
	virtual void write(const std::string &tw);
	virtual void write(const char* buf, size_t count, ostream_type_t stream = STDOUT);
	std::string getData(void);

private:
	std::string data;
};