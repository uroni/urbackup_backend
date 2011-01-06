/////////////////////////////////////////////////////////////////////////////
// CEvent

#include <assert.h>


class CEvent
{
public:
   HANDLE m_hEvent;

   CEvent(HANDLE hEvent = INVALID_HANDLE_VALUE) : m_hEvent(hEvent)
   { 
	Create(NULL,TRUE,FALSE, NULL);
	
   }
   ~CEvent()
   {
      Close();
   }
   BOOL Create(LPCTSTR pstrName = NULL, BOOL bManualReset = FALSE, BOOL bInitialState = FALSE, LPSECURITY_ATTRIBUTES pEventAttributes = NULL)
   {
      assert(pstrName==NULL || !::IsBadStringPtr(pstrName,-1));
      assert(m_hEvent==INVALID_HANDLE_VALUE);
      m_hEvent = ::CreateEvent(pEventAttributes, bManualReset, bInitialState, pstrName);
      assert(m_hEvent!=INVALID_HANDLE_VALUE);
      return m_hEvent != INVALID_HANDLE_VALUE;
   }
   BOOL Open(LPCTSTR pstrName, DWORD dwDesiredAccess = EVENT_ALL_ACCESS, BOOL bInheritHandle = TRUE)
   {
      assert(!::IsBadStringPtr(pstrName,-1));
      assert(m_hEvent==INVALID_HANDLE_VALUE);
      m_hEvent = ::OpenEvent(dwDesiredAccess, bInheritHandle, pstrName);
      assert(m_hEvent!=INVALID_HANDLE_VALUE);
      return m_hEvent != INVALID_HANDLE_VALUE;
   }
   BOOL IsOpen() const
   {
      return m_hEvent != INVALID_HANDLE_VALUE;
   }
   void Close()
   {
      if( m_hEvent == INVALID_HANDLE_VALUE ) return;
      ::CloseHandle(m_hEvent);
      m_hEvent = INVALID_HANDLE_VALUE;
   }
   void Attach(HANDLE hEvent)
   {
      assert(m_hEvent==INVALID_HANDLE_VALUE);
      m_hEvent= hEvent;
   }  
   HANDLE Detach()
   {
      HANDLE hEvent = m_hEvent;
      m_hEvent = INVALID_HANDLE_VALUE;
      return hEvent;
   }
   BOOL ResetEvent()
   {
      assert(m_hEvent!=INVALID_HANDLE_VALUE);
      return ::ResetEvent(m_hEvent);
   }
   BOOL SetEvent()
   {
      assert(m_hEvent!=INVALID_HANDLE_VALUE);
      return ::SetEvent(m_hEvent);
   }
   BOOL PulseEvent()
   {
      assert(m_hEvent!=INVALID_HANDLE_VALUE);
      return ::PulseEvent(m_hEvent);
   }
   BOOL IsSignalled() const
   {
      assert(m_hEvent!=INVALID_HANDLE_VALUE);
      return ::WaitForSingleObject(m_hEvent, 0) == WAIT_OBJECT_0;
   }
   BOOL WaitForEvent(DWORD dwTimeout = INFINITE)
   {
      assert(m_hEvent!=INVALID_HANDLE_VALUE);
      return ::WaitForSingleObject(m_hEvent, dwTimeout) == WAIT_OBJECT_0;
   }
   operator HANDLE() const { return m_hEvent; }
};