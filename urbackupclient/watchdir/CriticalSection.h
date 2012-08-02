#include <windows.h>

class CCriticalSection
{
public:
    CCriticalSection();
    ~CCriticalSection();

    void Enter();

    BOOL Lock(){ Enter(); return TRUE; }
    BOOL Unlock(){ Leave(); return TRUE; }

    bool TryEnter();

    void Leave();

private:
    CRITICAL_SECTION cs;
};

class CSingleLock
{
public:
        CSingleLock(CCriticalSection *pCS, BOOL parm)
        {
                cs=pCS;
                cs->Lock();
        }
        ~CSingleLock()
        {
                cs->Unlock();
        }
private:
      CCriticalSection *cs;
};
