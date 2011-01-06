#ifndef IPIPEDPROCESS_H
#define IPIPEDPROCESS_H

#include "../Interface/Object.h"

class IPipe;

class IPipedProcess : public IObject
{
public:
	virtual IPipe *getOutputPipe()=0;
	virtual bool isOpen()=0;
	virtual bool Write(const std::string &str)=0;
};

#endif //IPIPEDPROCESS_H