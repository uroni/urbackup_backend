#ifndef CRITICAL_SECTION_H
#define CRITICAL_SECTION_H

#pragma warning ( disable:4005 )
#pragma warning ( disable:4996 )

#include "../Interface/Mutex.h"


class CriticalSection
{
public:
    CriticalSection();
    ~CriticalSection();

    void Enter();

    //bool TryEnter();

    void Leave();

private:
    //CRITICAL_SECTION cs;
	IMutex *mutex;
};

#endif
