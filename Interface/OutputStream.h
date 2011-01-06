#include <string>

enum ostream_type_t
    { STDOUT
    , STDERR
    };

class IOutputStream
{
public:
    virtual void write(const std::string &tw)=0;
	virtual void write(const char* buf, size_t count, ostream_type_t stream = STDOUT)=0;
};