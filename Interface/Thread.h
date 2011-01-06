#ifndef INTERFACE_THREAD_H
#define INTERFACE_THREAD_H

class IThread
{
public:
	virtual void operator()(void)=0;
};

#endif //INTERFACE_THREAD_H
