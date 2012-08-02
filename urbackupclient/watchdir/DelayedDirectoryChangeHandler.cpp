// DelayedDirectoryChangeHandler.cpp: implementation of the CDelayedDirectoryChangeHandler2 class.
//
//////////////////////////////////////////////////////////////////////

#include "DirectoryChanges.h"
#include "DelayedDirectoryChangeHandler.h"
#include <process.h>//for _beginthreadex

#include <shlwapi.h>				 // for PathMatchSpec
//#pragma comment( lib, "shlwapi.lib") // function
#include <tchar.h>


#define UWM_DELAYED_DIRECTORY_NOTIFICATION (WM_APP+1024)
#pragma warning ( disable : 4996 )


HINSTANCE GetInstanceHandle()
{
	return (HINSTANCE)GetModuleHandle(NULL);
	// ASSERT( AfxGetInstanceHandle() == (HINSTANCE)GetModuleHandle(NULL) ); <-- true for building .exe's 
	//NOTE: In Dll's using shared MFC, AfxGetInstanceHandle() != (HINSTANCE)GetModuleHandle(NULL)...
	//don't know if this is the case for dll's using static MFC
}
static inline bool IsEmptyString(LPCTSTR sz)
{
	return (bool)(sz==NULL || *sz == 0);
}
/*********************************************************
  PathMatchSpec() requires IE 4.0 or greater on NT...
  if running on NT 4.0 w/ out IE 4.0, then uses this function instead.

  Based on code by Jack Handy:
  http://www.codeproject.com/string/wildcmp.asp

  Changed slightly to match the PathMatchSpec signature, be unicode compliant & to ignore case by myself.
  
*********************************************************/

#define _TESTING_WILDCMP_ONLY_ 

BOOL STDAPICALLTYPE wildcmp(LPCTSTR string, LPCTSTR wild ) 
{
	const TCHAR *cp, *mp;
	cp = mp = NULL;
	
	while ((*string) && (*wild != ('*'))) 
	{
		if ((_toupper(*wild) != _toupper(*string)) && (*wild != ('?'))) 
		{
			return FALSE;
		}
		wild++;
		string++;
	}
		
	while (*string) 
	{
		if (*wild == ('*')) 
		{
			if (!*++wild) 
			{
				return TRUE;
			}
			mp = wild;
			cp = string+1;
		} 
		else 
		if ((_toupper(*wild) == _toupper(*string)) || (*wild == ('?'))) 
		{
			wild++;
			string++;
		} 
		else 
		{
			wild = mp;
			string = cp++;
		}
	}
		
	while (*wild == ('*')) 
	{
		wild++;
	}
	return (!*wild)? TRUE : FALSE;
}

//////////////////////////////////////////////////////////////////////////
//
//CDirChangeNotification member functions:
//
CDirChangeNotification::CDirChangeNotification(CDelayedDirectoryChangeHandler *	pDelayedHandler, DWORD dwPartialPathOffset)
:m_pDelayedHandler( pDelayedHandler )
,m_szFileName1(NULL)
,m_szFileName2(NULL)
,m_dwError(0UL)
,m_dwPartialPathOffset(dwPartialPathOffset)
{
	assert( pDelayedHandler );
}

CDirChangeNotification::~CDirChangeNotification()
{
	if( m_szFileName1 ) free(m_szFileName1), m_szFileName1 = NULL;
	if( m_szFileName2 ) free(m_szFileName2), m_szFileName2 = NULL;
}

void CDirChangeNotification::DispatchNotificationFunction()
{
	assert( m_pDelayedHandler );
	if( m_pDelayedHandler )
		m_pDelayedHandler->DispatchNotificationFunction( this );
}

void CDirChangeNotification::PostOn_FileAdded(LPCTSTR szFileName)
{
	assert( szFileName );
	m_eFunctionToDispatch	= eOn_FileAdded;
	m_szFileName1			= _tcsdup( szFileName) ;
	//
	// post the message so it'll be dispatch by another thread.
	PostNotification();

}
void CDirChangeNotification::PostOn_FileRemoved(LPCTSTR szFileName)
{
	assert( szFileName );
	m_eFunctionToDispatch	= eOn_FileRemoved;
	m_szFileName1			= _tcsdup( szFileName) ;
	//
	// post the message so it'll be dispatched by another thread.
	PostNotification();
	
}
void CDirChangeNotification::PostOn_FileNameChanged(LPCTSTR szOldName, LPCTSTR szNewName)
{
	assert( szOldName && szNewName );

	m_eFunctionToDispatch	= eOn_FileNameChanged;
	m_szFileName1			= _tcsdup( szOldName) ;
	m_szFileName2			= _tcsdup( szNewName) ;
	//
	// post the message so it'll be dispatched by another thread.
	PostNotification();
	
}

void CDirChangeNotification::PostOn_FileModified(LPCTSTR szFileName)
{
	assert( szFileName );

	m_eFunctionToDispatch	= eOn_FileModified;
	m_szFileName1			= _tcsdup( szFileName );
	//
	// post the message so it'll be dispatched by another thread.
	PostNotification();
}

void CDirChangeNotification::PostOn_ReadDirectoryChangesError(DWORD dwError, LPCTSTR szDirectoryName)
{
	assert( szDirectoryName );

	m_eFunctionToDispatch = eOn_ReadDirectoryChangesError;
	m_dwError			  = dwError;
	m_szFileName1		  = _tcsdup(szDirectoryName);
	//
	// post the message so it'll be dispatched by the another thread.
	PostNotification();
	
}

void CDirChangeNotification::PostOn_WatchStarted(DWORD dwError, LPCTSTR szDirectoryName)
{
	assert( szDirectoryName );

	m_eFunctionToDispatch = eOn_WatchStarted;
	m_dwError			  =	dwError;
	m_szFileName1		  = _tcsdup(szDirectoryName);

	PostNotification();
}

void CDirChangeNotification::PostOn_WatchStopped(LPCTSTR szDirectoryName)
{
	assert( szDirectoryName );

	m_eFunctionToDispatch = eOn_WatchStopped;
	m_szFileName1		  = _tcsdup(szDirectoryName);

	PostNotification();
}

void CDirChangeNotification::PostNotification()
{
	assert( m_pDelayedHandler );
	if( m_pDelayedHandler )
		m_pDelayedHandler->PostNotification( this );
}

static LRESULT CALLBACK DelayedNotificationWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
//
//	This is the wndproc for the notification window
//
//	it's here to dispatch the notifications to the client
//
{
	if( message == UWM_DELAYED_DIRECTORY_NOTIFICATION )
	{
		CDirChangeNotification * pNotification = reinterpret_cast<CDirChangeNotification*>(lParam);
		assert(  pNotification );
		if( pNotification )
		{
			DWORD dwEx(0);
			__try{
				pNotification->DispatchNotificationFunction();
			}
			__except(dwEx = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER){
				//An exception was raised:
				//
				//	Likely cause: there was a problem creating the CDelayedDirectoryChangeHandler::m_hWatchStoppedDispatchedEvent object
				//	and the change handler object was deleted before the notification could be dispatched to this function.
				//
				//  or perhaps, somebody's implementation of an overridden function caused an exception
				TRACE4(_T("Following exception occurred: %d -- File: %s Line: %d\n"), dwEx, _T(__FILE__), __LINE__);
			}
		}
		
		return 0UL;
	}
	else
		return DefWindowProc(hWnd,message,wParam,lParam);
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
//
//CDelayedNotificationWindow static member vars:
//
long CDelayedNotificationWindow::s_nRefCnt = 0L;
HWND CDelayedNotificationWindow::s_hWnd = NULL;
BOOL CDelayedNotificationWindow::s_bRegisterWindow = FALSE;
//
//
long CDelayedNotificationWindow::AddRef()//creates window for first time if necessary
{
	if( InterlockedIncrement(&s_nRefCnt) == 1
		||	!::IsWindow( s_hWnd ) )
	{
		TRACE1(_T("CDelayedNotificationWindow -- Creating the notification window\n"));
		CreateNotificationWindow();
	}
	return s_nRefCnt;
}

long CDelayedNotificationWindow::Release()//destroys window for last time if necessary
{
	long nRefCnt = -1;
	if( (nRefCnt = InterlockedDecrement(&s_nRefCnt)) == 0 )
	{
		//no body else using the window so destroy it?
		TRACE1(_T("CDelayedNotificationWindow -- Destroying the notification window\n"));
		DestroyWindow( s_hWnd );
		s_hWnd = NULL;
	}
	return nRefCnt;
}
BOOL CDelayedNotificationWindow::RegisterWindowClass(LPCTSTR szClassName)
//
//	registers our own window class to use as the hidden notification window.
//
{
	WNDCLASS wc = {0};
	
	wc.style = 0;
	wc.hInstance		= GetInstanceHandle();
	wc.lpszClassName	= szClassName;
	wc.hbrBackground	= (HBRUSH)GetStockObject( WHITE_BRUSH );
	wc.lpfnWndProc		= DelayedNotificationWndProc;
	
	ATOM ant = RegisterClass( &wc );
	if( ant == NULL )
	{
		TRACE(_T("CDirChangeNotification::RegisterWindowClass - RegisterClass failed: %d\n"), GetLastError());
	}
	return (BOOL)(ant!= NULL);
	
}

BOOL CDelayedNotificationWindow::CreateNotificationWindow()
//
//	Create the hidden notification windows.
//
{
	TCHAR szClassName[] = _T("Delayed_Message_Sender");
	if( !s_bRegisterWindow )
		s_bRegisterWindow = RegisterWindowClass(szClassName);
	s_hWnd 	= CreateWindowEx(0, szClassName, _T("DelayedWnd"),0,0,0,0,0, NULL, 0, 
							GetInstanceHandle(), NULL);
	if( s_hWnd == NULL )
	{
		TRACE(_T("Unable to create notification window! GetLastError(): %d\n"), GetLastError());
		TRACE3(_T("File: %s Line: %d\n"), _T(__FILE__), __LINE__);
	}
	
	return (BOOL)(s_hWnd != NULL);
}
void CDelayedNotificationWindow::PostNotification(CDirChangeNotification * pNotification)
//
//	Posts a message to a window created in the main 
//	thread.
//	The main thread catches this message, and dispatches it in 
//	the context of the main thread.
//
{
	assert( pNotification );
	assert( s_hWnd );
	assert( ::IsWindow( s_hWnd ) );

	PostMessage(s_hWnd, 
				UWM_DELAYED_DIRECTORY_NOTIFICATION, 
				0, 
				reinterpret_cast<LPARAM>( pNotification ));

//  if you don't want the notification delayed, 
//  
//	if( false )
//	{
//		pNotification->DispatchNotificationFunction();
//	}
}

/////////////////////////////////////////////////////////
//	CDelayedNoticationThread
//
long	CDelayedNotificationThread::s_nRefCnt = 0L;
HANDLE	CDelayedNotificationThread::s_hThread = NULL;
DWORD	CDelayedNotificationThread::s_dwThreadID = 0UL;

void CDelayedNotificationThread::PostNotification(CDirChangeNotification * pNotification)
{
	assert( s_hThread != NULL );
	assert( s_dwThreadID != 0 );

	if(
		!PostThreadMessage(s_dwThreadID, 
						   UWM_DELAYED_DIRECTORY_NOTIFICATION, 
						   0, 
						   reinterpret_cast<LPARAM>(pNotification))
	  )
	{
		//Note, this can sometimes fail.
		//Will fail if: s_dwThreadID references a invalid thread id(the thread has died for example)
		// OR will fail if the thread doesn't have a message queue.
		//
		//	This was failing because the thread had not been fully started by the time PostThreadMessage had been called
		//
		//Note: if this fails, it creates a memory leak because
		//the CDirChangeNotification object that was allocated and posted
		//to the thread is actually never going to be dispatched and then deleted.... it's
		//hanging in limbo.....

		//
		//	The fix for this situation was to force the thread that starts
		//	this worker thread to wait until the worker thread was fully started before
		//	continueing.  accomplished w/ an event... also.. posting a message to itself before signalling the 
		//  'spawning' thread that it was started ensured that there was a message pump
		//  associated w/ the worker thread by the time PostThreadMessage was called.
		TRACE4(_T("PostThreadMessage() failed while posting to thread id: %d! GetLastError(): %d%s\n"), s_dwThreadID, GetLastError(), GetLastError() == ERROR_INVALID_THREAD_ID? _T("(ERROR_INVALID_THREAD_ID)") : _T(""));
	}
}

bool CDelayedNotificationThread::StartThread()
{
	TRACE1(_T("CDelayedNotificationThread::StartThread()\n"));
	assert( s_hThread == NULL 
		&&	s_dwThreadID == 0 );
	s_hThread = (HANDLE)_beginthreadex(NULL,0, 
								ThreadFunc, this, 0, (UINT*) &s_dwThreadID);
	if( s_hThread )
		WaitForThreadStartup();

	return s_hThread == NULL ? false : true;

}

bool CDelayedNotificationThread::StopThread()
{
	TRACE1(_T("CDelayedNotificationThread::StopThread()\n"));
	if( s_hThread != NULL 
	&&	s_dwThreadID != 0 )
	{
		PostThreadMessage(s_dwThreadID, WM_QUIT, 0,0);

		WaitForSingleObject(s_hThread, INFINITE);
		CloseHandle(s_hThread);
		s_hThread	 = NULL;
		s_dwThreadID = 0UL;
		return true;
	}
	return true;//already shutdown
}

UINT __stdcall CDelayedNotificationThread::ThreadFunc(LPVOID lpvThis)
{
	//UNREFERENCED_PARAMETER( lpvThis );
	//
	//	Implements a simple message pump
	//
	CDelayedNotificationThread * pThis = reinterpret_cast<CDelayedNotificationThread*>(lpvThis);
	assert( pThis );

	//
	//	Insure that this thread has a message queue by the time another
	//	thread gets control and tries to use PostThreadMessage
	//	problems can happen if someone tries to use PostThreadMessage
	//	in between the time pThis->SignalThreadStartup() is called,
	//	and the first call to GetMessage();

	::PostMessage(NULL, WM_NULL, 0,0);//if this thread didn't have a message queue before this, it does now.


	//
	//
	//	Signal that this thread has started so that StartThread can continue.
	//
	if( pThis ) pThis->SignalThreadStartup();

	TRACE(_T("CDelayedNotificationThread::ThreadFunc() ThreadID: %d -- Starting\n"), GetCurrentThreadId());
	MSG msg;
	do{
		while( GetMessage(&msg, NULL, 0,0) )//note GetMessage() can return -1, but only if i give it a bad HWND.(HWND for another thread for example)..i'm not giving an HWND, so no problemo here.
		{
			if( msg.message == UWM_DELAYED_DIRECTORY_NOTIFICATION )
			{
				CDirChangeNotification * pNotification = 
								reinterpret_cast<CDirChangeNotification *>( msg.lParam );
				DWORD dwEx(0UL);

				__try{
				if( pNotification )
					pNotification->DispatchNotificationFunction();
				}
				__except(dwEx = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER){
				//An exception was raised:
				//
				//	Likely causes: 
				//		* There was a problem creating the CDelayedDirectoryChangeHandler::m_hWatchStoppedDispatchedEvent object
				//			and the change handler object was deleted before the notification could be dispatched to this function.
				//
				//		* Somebody's implementation of an overridden virtual function caused an exception
				TRACE4(_T("The following exception occurred: %d -- File: %s Line: %d\n"), dwEx, _T(__FILE__), __LINE__);
				}
			}
			else
			if( msg.message == WM_QUIT )
			{
				break;
			}
		}
	}while( msg.message != WM_QUIT );
	TRACE(_T("CDelayedNotificationThread::ThreadFunc() exiting. ThreadID: %d\n"), GetCurrentThreadId());
	return 0;
}

long CDelayedNotificationThread::AddRef()
{
	if( InterlockedIncrement(&s_nRefCnt) == 1 )
	{
		StartThread();
	}
	return s_nRefCnt;
}
long CDelayedNotificationThread::Release()
{
	if( InterlockedDecrement(&s_nRefCnt) <= 0 )
	{
		s_nRefCnt = 0;
		StopThread();
	}
	return s_nRefCnt;
}

///////////////////////////////////////////////////////
//static member data for CDelayedDirectoryChangeHandler
HINSTANCE CDelayedDirectoryChangeHandler::s_hShlwapi_dll = NULL;//for the PathMatchSpec() function
BOOL CDelayedDirectoryChangeHandler::s_bShlwapi_dllExists = TRUE;
long CDelayedDirectoryChangeHandler::s_nRefCnt_hShlwapi = 0L;
FUNC_PatternMatchSpec CDelayedDirectoryChangeHandler::s_fpPatternMatchSpec = wildcmp;//default
///////////////////////////////////////////////////////
//construction destruction
CDelayedDirectoryChangeHandler::CDelayedDirectoryChangeHandler(CDirectoryChangeHandler * pRealHandler, bool bAppHasGUI, LPCTSTR szIncludeFilter, LPCTSTR szExcludeFilter, DWORD dwFilterFlags)
: m_pDelayNotifier( NULL )
 ,m_pRealHandler( pRealHandler )
 ,m_szIncludeFilter(NULL)
 ,m_szExcludeFilter(NULL)
 ,m_dwFilterFlags( dwFilterFlags )
 ,m_dwPartialPathOffset( 0UL )
 ,m_hWatchStoppedDispatchedEvent(NULL)
 ,m_nNumIncludeFilterSpecs(0)
 ,m_nNumExcludeFilterSpecs(0)
{


	assert( m_pRealHandler ); 

	InitializePathMatchFunc( szIncludeFilter, szExcludeFilter );

	//
	// See that we're 
	//


	m_hWatchStoppedDispatchedEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);//AUTO-RESET, not initially signalled
	assert( m_hWatchStoppedDispatchedEvent );
	
	if( bAppHasGUI )
	{
		//
		//	The value true was passed to the CDirectoryChangeWatcher constructor.
		//	It's assumed that your app has a gui, that is, it implements
		//	a message pump.  To delay the notification to another thread,
		//	we'll use a hidden notification window.
		//
		m_pDelayNotifier = new CDelayedNotificationWindow();
	}
	else
	{
		// The value 'false' was passed to the CDirectoryChangeWatcher constructor.
		//
		// Your app has no message pump... use a class that implements one for you
		// in a worker thread.
		//
		// Notifications will be executed in this worker thread.
		//
		m_pDelayNotifier = new CDelayedNotificationThread();
	}
}

CDelayedDirectoryChangeHandler::~CDelayedDirectoryChangeHandler()
{
	if( m_pRealHandler )
		delete m_pRealHandler, m_pRealHandler = NULL;
	if( m_pDelayNotifier )
		delete m_pDelayNotifier, m_pDelayNotifier = NULL;

	if( m_hWatchStoppedDispatchedEvent )
		CloseHandle(m_hWatchStoppedDispatchedEvent), m_hWatchStoppedDispatchedEvent = NULL;

	if( m_szIncludeFilter ){
		if( m_nNumIncludeFilterSpecs == 1 )
			free(m_szIncludeFilter);
		else
		{
			TCHAR ** ppTmp = (TCHAR**)m_szIncludeFilter;
			for(int i(0); i < m_nNumIncludeFilterSpecs; ++i)
			{
				free( *ppTmp++ );
			}
			free( m_szIncludeFilter );
		}
		m_szIncludeFilter = NULL;
		m_nNumIncludeFilterSpecs;
	}
	if( m_szExcludeFilter ) {
		if( m_nNumExcludeFilterSpecs == 1 )
			free(m_szExcludeFilter);
		else{
			TCHAR ** ppTmp = (TCHAR**)m_szExcludeFilter;
			for(int i(0); i < m_nNumExcludeFilterSpecs; ++i)
			{
				free( *ppTmp++ );
			}
			free( m_szExcludeFilter );
		}
		m_szExcludeFilter = NULL;
		m_nNumExcludeFilterSpecs = 0;
	}

	UninitializePathMatchFunc();
}

BOOL CDelayedDirectoryChangeHandler::_PathMatchSpec(LPCTSTR szPath, LPCTSTR szPattern)
{
	if( s_fpPatternMatchSpec )
	{
		return s_fpPatternMatchSpec(szPath, szPattern);
	}
	assert( FALSE );
	return TRUE;//everything matches.
}

BOOL CDelayedDirectoryChangeHandler::InitializePathMatchFunc(LPCTSTR szIncludeFilter, LPCTSTR szExcludeFilter)
//
//
//	To support the Include and Exclude filters, the PathMatchSpec function is used.
//	PathMatchSpec is only supported on NT4.0 if IE 4.0 is installed.
//
//	for the case where this code is running on NT 4.0 w/out IE 4.0, we use
//	a different function: wildcmp ()
//
//
//	This function attempts to load shlwapi.dll dynamically and find the PathMatchSpec function.
//
//	if the function PathMatchSpec can't be found, the function pointer s_fpPathMatchSpec is set to wildcmp.
//
//
//	Note:  wildcmp doesn't support multiple file specs separated by a semi-colon
//	as PathMatchSpec does.... we'll support it by parsing them 
//	when we want to test the filters, we'll call the pattern matching functions multiple times...
//
{

	//
	//	Copy the include/exclude filters if specified...
	//
	//
	if( IsEmptyString(szIncludeFilter) 
	&&	IsEmptyString(szExcludeFilter) )
	{
		return TRUE;//both the include && exclude filters aren't specified
					//no need to initialize the pattern matching function....
					//if filters are never used, then
					//one less dll is loaded.
	}

#ifdef _TESTING_WILDCMP_ONLY_
	s_hShlwapi_dll = NULL;
	s_bShlwapi_dllExists = FALSE;
	return InitializePatterns(szIncludeFilter, szExcludeFilter);
#endif


	if( s_hShlwapi_dll != NULL )
	{
		assert( s_fpPatternMatchSpec != NULL );
		InterlockedIncrement(&s_nRefCnt_hShlwapi);

		return InitializePatterns(szIncludeFilter, szExcludeFilter);
	}
	else{
		if( s_bShlwapi_dllExists == TRUE )//either the dll exists, or we haven't tried loading it yet...
		{
			//The pattern match function hasn't been initialized yet....
			//
		
			s_hShlwapi_dll = ::LoadLibrary(_T("Shlwapi.dll"));
			if( s_hShlwapi_dll == NULL )
			{
				s_bShlwapi_dllExists = FALSE;//don't try loading this dll again.
				s_fpPatternMatchSpec = wildcmp;//even though it's set buy default, and this code will only get here once, set it just for fun.

				return InitializePatterns(szIncludeFilter, szExcludeFilter);
				
			}
			else
			{
				//Shlwapi.dll was found....check it for PathMatchSpec()
#ifdef UNICODE 
				s_fpPatternMatchSpec = (FUNC_PatternMatchSpec)::GetProcAddress(s_hShlwapi_dll, "PathMatchSpecW");
#else
				s_fpPatternMatchSpec = (FUNC_PatternMatchSpec)::GetProcAddress(s_hShlwapi_dll, "PathMatchSpecA");
#endif
	
				if( s_fpPatternMatchSpec != NULL )
				{
					//UsesRealPathMatchSpec() will now return true.
					//we're on NT w/ IE 4.0 or greater...(or Win2k/XP)
					InterlockedIncrement(&s_nRefCnt_hShlwapi);
					return InitializePatterns(szIncludeFilter, szExcludeFilter);
				}
				else
				{
					//we found shlwapi.dll, but it didn't have PathMatchSpec()
					::FreeLibrary( s_hShlwapi_dll );
					s_hShlwapi_dll = NULL;
					s_bShlwapi_dllExists = FALSE;

					//instead of using PathMatchSpec()
					//we'll use wildcmp()
					s_fpPatternMatchSpec = wildcmp;
					//UsesRealPathMatchSpec() will now return false w/out asserting.

					return InitializePatterns(szIncludeFilter, szExcludeFilter);
				}
			}
			
		}
	}
	return (s_fpPatternMatchSpec != NULL);
}

BOOL CDelayedDirectoryChangeHandler::InitializePatterns(LPCTSTR szIncludeFilter, LPCTSTR szExcludeFilter)
{
	assert( !IsEmptyString(szIncludeFilter)   //one of these must have something in it, 
		||  !IsEmptyString(szExcludeFilter) );//or else this function shouldn't be called.

	if( s_hShlwapi_dll != NULL )
	{
		//we're using Shlwapi.dll's PathMatchSpec function....
		//we're running on NT4.0 w/ IE 4.0 installed, or win2k/winXP(or greater)
		//
		//	Copy the include/exclude filters if specified...
		//
		//
		// we're using the real PathMatchSpec() function which
		//	supports multiple pattern specs...(separated by a semi-colon)
		//	so there's only one filter spec as far as my code is concerned.
		//
		if( !IsEmptyString(szIncludeFilter) )
		{
			m_szIncludeFilter = _tcsdup(szIncludeFilter);
			assert( m_szIncludeFilter );
			m_nNumIncludeFilterSpecs = 1;
		}
		if( !IsEmptyString(szExcludeFilter) )
		{
			m_szExcludeFilter = _tcsdup(szExcludeFilter);	
			assert( m_szExcludeFilter );
			m_nNumExcludeFilterSpecs = 1;
		}	
	}
	else
	{
		//shlwapi.dll isn't on this machine.... can happen on NT4.0 w/ out IE 4.0 installed.
		assert( s_bShlwapi_dllExists == FALSE );

		//
		//	we're using the function wildcmp() instead of PathMatchSpec..
		//
		//	this means that multiple pattern specs aren't supported...
		// in order to support them, we'll tokenize the string into multiple
		// pattern specs and test the string multiple times(once per pattern spec) 
		// in order to support multiple patterns.
		//
		//
		//	m_szIncludeFilter & m_szExclude filter will be used like TCHAR**'s instead of TCHAR*'s
		//

		m_nNumIncludeFilterSpecs = 0;
		if( !IsEmptyString(szIncludeFilter) )
		{
			TCHAR * szTmpFilter = _tcsdup(szIncludeFilter);
			TCHAR * pTok = _tcstok( szTmpFilter, _T(";"));
			while( pTok )
			{
				m_nNumIncludeFilterSpecs++;
				pTok = _tcstok(NULL, _T(";"));
			}
			if( m_nNumIncludeFilterSpecs == 1 )
				m_szIncludeFilter = _tcsdup(szIncludeFilter);
			else
			{   //allocate room for pointers .. one for each token...
				m_szIncludeFilter = (TCHAR*)malloc( m_nNumIncludeFilterSpecs * sizeof(TCHAR*));

				free(szTmpFilter);
				szTmpFilter = _tcsdup(szIncludeFilter);
				pTok = _tcstok(szTmpFilter, _T(";"));
				TCHAR ** ppTmp = (TCHAR**)m_szIncludeFilter;
				while(pTok)
				{
					*ppTmp = _tcsdup(pTok);
					ppTmp++;
					pTok = _tcstok(NULL, _T(";"));
				}
			}

			free(szTmpFilter);
		}

		//
		//	Do the same for the Exclude filter...
		//
		m_nNumExcludeFilterSpecs = 0;
		if( !IsEmptyString(szExcludeFilter) )
		{
			TCHAR * szTmpFilter = _tcsdup(szExcludeFilter);
			TCHAR * pTok = _tcstok( szTmpFilter, _T(";"));
			while( pTok )
			{
				m_nNumExcludeFilterSpecs++;
				pTok = _tcstok(NULL, _T(";"));
			}
			if( m_nNumExcludeFilterSpecs == 1 )
				m_szExcludeFilter = _tcsdup(szExcludeFilter);
			else
			{   //allocate room for pointers .. one for each token...
				m_szExcludeFilter = (TCHAR*)malloc( m_nNumExcludeFilterSpecs * sizeof(TCHAR*));

				free(szTmpFilter);
				szTmpFilter = _tcsdup(szExcludeFilter);

				pTok = _tcstok(szTmpFilter, _T(";"));
				TCHAR ** ppTmp = (TCHAR**)m_szExcludeFilter;
				while(pTok)
				{
					*ppTmp = _tcsdup(pTok);
					ppTmp++;
					pTok = _tcstok(NULL, _T(";"));
				}
			}
			free(szTmpFilter);
		}

	}

	return (m_szExcludeFilter!= NULL || (m_szIncludeFilter != NULL));
}
void CDelayedDirectoryChangeHandler::UninitializePathMatchFunc()
{
	if( s_bShlwapi_dllExists == TRUE 
	&&  s_hShlwapi_dll != NULL )
	{
		if( InterlockedDecrement(&s_nRefCnt_hShlwapi) <= 0)
		{
			s_nRefCnt_hShlwapi = 0;
			FreeLibrary( s_hShlwapi_dll );
			s_hShlwapi_dll = NULL;
			s_fpPatternMatchSpec = wildcmp;
		}
	}
}

bool CDelayedDirectoryChangeHandler::UsesRealPathMatchSpec() const
//are we using PathMatchSpec() or wildcmp()?
{
	if( s_hShlwapi_dll != NULL && s_fpPatternMatchSpec != NULL )
		return true;
	if( s_hShlwapi_dll == NULL && s_fpPatternMatchSpec != NULL )
		return false;

	assert( FALSE );//this function was called before InitializePathMatchFunc()
	//oops!
	return false;
}
static inline bool HasTrailingBackslash(const std::wstring & str )
{
	if( str.size() > 0
	&&	str[ str.size() - 1 ] == ('\\') )
		return true;
	return false;
}
void CDelayedDirectoryChangeHandler::SetPartialPathOffset(const std::wstring & strWatchedDirName)
{
	if( m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_CHECK_PARTIAL_PATH )
	{
		//set the offset to 
		if( HasTrailingBackslash( strWatchedDirName ) )
			m_dwPartialPathOffset = (DWORD)strWatchedDirName.size();
		else
			m_dwPartialPathOffset = (DWORD)strWatchedDirName.size() + 1;
	}
	else
		m_dwPartialPathOffset = 0;
}

CDirChangeNotification * CDelayedDirectoryChangeHandler::GetNotificationObject()
//
//	Maybe in future I'll keep a pool of these 
//	objects around to increase performance...
//	using objects from a cache will be faster 
//	than allocated and destroying a new one each time.
//	
//  
{
	assert( m_pRealHandler );
	return new CDirChangeNotification(this, m_dwPartialPathOffset);//helps support FILTERS_CHECK_PARTIAL_PATH
}

void CDelayedDirectoryChangeHandler::DisposeOfNotification(CDirChangeNotification * pNotification)
{
	delete pNotification;
}

//These functions are called when the directory to watch has had a change made to it
void CDelayedDirectoryChangeHandler::On_FileAdded(const std::wstring & strFileName)
{
	CDirChangeNotification * p = GetNotificationObject();
	assert( p );
	if( p ) p->PostOn_FileAdded( strFileName.c_str() );
}

void CDelayedDirectoryChangeHandler::On_FileRemoved(const std::wstring & strFileName)
{
	CDirChangeNotification * p = GetNotificationObject();
	assert( p );
	if( p ) p->PostOn_FileRemoved( strFileName.c_str() );
}

void CDelayedDirectoryChangeHandler::On_FileModified(const std::wstring & strFileName)
{
	CDirChangeNotification * p = GetNotificationObject();
	assert( p );
	if( p ) p->PostOn_FileModified( strFileName .c_str());
}

void CDelayedDirectoryChangeHandler::On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName)
{
	CDirChangeNotification * p = GetNotificationObject();	
	assert( p );
	if( p ) p->PostOn_FileNameChanged( strOldFileName.c_str(), strNewFileName.c_str() );
}

void CDelayedDirectoryChangeHandler::On_ReadDirectoryChangesError(DWORD dwError, const std::wstring & strDirectoryName)
{
	CDirChangeNotification * p = GetNotificationObject();
	assert( p );
	if( p ) p->PostOn_ReadDirectoryChangesError( dwError, strDirectoryName.c_str() );
}

void CDelayedDirectoryChangeHandler::On_WatchStarted(DWORD dwError, const std::wstring & strDirectoryName)
{
	if( !(m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_NO_WATCHSTART_NOTIFICATION))
	{
		CDirChangeNotification * p = GetNotificationObject();

		if( p ) p->PostOn_WatchStarted(dwError, strDirectoryName.c_str());
	}
}

void CDelayedDirectoryChangeHandler::On_WatchStopped(const std::wstring & strDirectoryName)
{
	if( !(m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_NO_WATCHSTOP_NOTIFICATION))
	{
		CDirChangeNotification * p = GetNotificationObject();

		if( p ){
			if( m_hWatchStoppedDispatchedEvent )
				::ResetEvent(m_hWatchStoppedDispatchedEvent);

			p->PostOn_WatchStopped( strDirectoryName.c_str() );

			//	Wait that this function has been dispatched to the other thread
			//	before continueing.  This object may be getting deleted
			//	soon after this function returns, and before the function can be
			//	dispatched to the other thread....
			WaitForOnWatchStoppedDispatched();
		}
	}
}


void CDelayedDirectoryChangeHandler::PostNotification(CDirChangeNotification * pNotification)
{
	if( m_pDelayNotifier )
		m_pDelayNotifier->PostNotification( pNotification );
}

inline bool IsNonFilterableEvent( CDirChangeNotification::eFunctionToDispatch eEvent)
// Helper function
//	For filtering events..... these functions can not be filtered out.
//
{
	if(	eEvent == CDirChangeNotification::eOn_WatchStarted 
	||	eEvent == CDirChangeNotification::eOn_WatchStopped
	||	eEvent == CDirChangeNotification::eOn_ReadDirectoryChangesError )
	{
		return true;
	}
	else
		return false;
}
DWORD GetPathOffsetBasedOnFilterFlags(CDirChangeNotification * pNot, DWORD dwFilterFlags)
{
	
	assert( pNot && dwFilterFlags != 0 );
	//helps support the filter options FILTERS_CHECK_FULL_PATH, FILTERS_CHECK_PARTIAL_PATH, and FILTERS_CHECK_FILE_NAME_ONLY

	DWORD dwFileNameOffset = 0;//offset needed to support FILTERS_CHECK_FULL_PATH
	if( dwFilterFlags & CDirectoryChangeWatcher::FILTERS_CHECK_FILE_NAME_ONLY )
	{
		//set the offset to support FILTERS_CHECK_FILE_NAME_ONLY
		TCHAR * pSlash  = _tcsrchr(pNot->m_szFileName1, _T('\\'));
		if( pSlash )
			dwFileNameOffset = (DWORD)(++pSlash - pNot->m_szFileName1);

		//
		//	Because file name change notifications take place in the same directory,
		//	the same dwFileNameOffset can be used for the szNewFileName(pNot->m_szFileName2)
		//	when checking the filter against the new file name.
		//
	}
	else
	if( dwFilterFlags & CDirectoryChangeWatcher::FILTERS_CHECK_PARTIAL_PATH)
	{
		//
		//	partial path offset is the offset 
		//	from the beginning of the file name, 
		//	to the end of the watched directory path...
		//	ie: If you're watching "C:\Temp"
		//		and the file C:\Temp\SubFolder\FileName.txt" is changed,
		//		the partial path offset will give you "SubFolder\FileName.txt"
		//		when this is checked against the include/exclude filter.
		//
		dwFileNameOffset = pNot->m_dwPartialPathOffset;
	}
	//else
	//if( m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_CHECK_FULL_PATH )
	//	dwFileNameOffset = 0;
	
	return dwFileNameOffset;
}

bool CDelayedDirectoryChangeHandler::NotifyClientOfFileChange(CDirChangeNotification * pNot)
//
//
//	Perform the tests to see if the client wants to be notified of this
//	file change notification.
//
//	Tests performed:
//
//	Event test:		Not all events can be filtered out.
//					On_ReadDirectoryChangesError
//					cannot be filtered out.
//	Filter flags test:  User can specify flags so that no tests are performed....all notifications are sent to the user.
//
//	Filter test:	Test the notification file name against include and exclude filters.
//
//					Only files changes matching the INCLUDE filter will be passed to the client.
//					By not specifying an include filter, all file changes are passed to the client.
//	
//				    Any files matching the EXCLUDE filter will not be passed to the client.
//
//
//					Note: For the file name change event:
//							If the old file name does not pass the tests for the include and exclude filter
//							but the NEW file name does pass the test for the filters, then the client IS notified.
//
//	Client test:	The CDirectoryChangeHandler derived class is given a chance to filter the event by calling
//					CDirectoryChangeHandler::On_FilterNotification()
//
//	RETURN VALUE:
//	If this function returns true, the notification function is called.
//	If it returns false, the notification is ignored.
//			The client is notified by calling CDirectoryChangeHandler's virtual functions On_FileAdded(),On_FileRemoved(), etc.
{
	assert( pNot );
	assert( m_pRealHandler );

	//
	//	Some events can't be ignored, or filtered out.
	//

	if( ((m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_DONT_USE_ANY_FILTER_TESTS) == CDirectoryChangeWatcher::FILTERS_DONT_USE_ANY_FILTER_TESTS)
	||	IsNonFilterableEvent( pNot->m_eFunctionToDispatch ) )
	{
		// Either this is a non-filterable event, or we're not using any filters...
		// client is notified of all events..
		return true;
	}

	//
	//	See if user wants to test CDirectoryChangeHandler::On_FilterNotification()
	//	before tests are performed against the file name, and filter specifications
	//
	if( (m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_TEST_HANDLER_FIRST )//specified that CDirectoryChangeHandler::On_FilterNotification is to be called before any other filter tests
	&&	!(m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_DONT_USE_HANDLER_FILTER)//and did not specify that this CDirectoryChangeHandler::On_FilterNotification is not to be called..
	&&	!m_pRealHandler->On_FilterNotification(pNot->m_eFunctionToDispatch, pNot->m_szFileName1, pNot->m_szFileName2) )
	{
		//
		//	Client specified to test handler first, and it didn't pass the test... don't notify the client.
		//
		return false;
	}
	//else
	//
	//	this file change passed the user test, continue testing
	//	to see if it passes the filter tests.

	DWORD dwFileNameOffset = GetPathOffsetBasedOnFilterFlags(pNot, m_dwFilterFlags);

	//
	//	See if the changed file matches the include or exclude filter
	//	Only allow notifications for included files 
	//	that have not been exluded.
	//
	if(!(m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_DONT_USE_FILTERS) )
	{
		if( false == IncludeThisNotification(pNot->m_szFileName1 + dwFileNameOffset)
		||	true == ExcludeThisNotification(pNot->m_szFileName1 + dwFileNameOffset) )
		{
			if( pNot->m_eFunctionToDispatch != CDirChangeNotification::eOn_FileNameChanged )
				return false;
			else{
				//Special case for file name change:
				//
				// the old file name didn't pass the include/exclude filter
				// but if the new name passes the include/exclude filter, 
				// we will pass it on to the client...

				if( false == IncludeThisNotification(pNot->m_szFileName2 + dwFileNameOffset) 
				||	true == ExcludeThisNotification(pNot->m_szFileName2 + dwFileNameOffset) )
				{
					// the new file name didn't pass the include/exclude filter test either
					// so don't pass the notification on...
					return false;
				}

			}
		}

	}
	
	//
	//	Finally, let the client determine whether or not it wants this file notification
	//	if this test has not already been performed...
	//

	if( (m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_TEST_HANDLER_FIRST) 
	||	(m_dwFilterFlags & CDirectoryChangeWatcher::FILTERS_DONT_USE_HANDLER_FILTER) )
	{
		//	if we got this far, and this flag was specified, 
		//	it's already passed this test,
		//  or we're not checking it based on the filter flag FILTERS_DONT_USE_HANDLER_FILTER....
		return true;
	}
	else
	{
		if( m_pRealHandler->On_FilterNotification(pNot->m_eFunctionToDispatch,
											  pNot->m_szFileName1,
											  pNot->m_szFileName2) )
		{
			return true;
		}
		else
		{
			//else  client's derived CDirectoryChangeHandler class chose 
			//		not to be notified of this file change
			return false;
		}
	}
		
	
}

bool CDelayedDirectoryChangeHandler::IncludeThisNotification(LPCTSTR szFileName)
//
//	The Include filter specifies which file names we should allow to notifications
//	for... otherwise these notifications are not dispatched to the client's code.
//
//	Tests the file name to see if it matches a filter specification
//
//	RETURN VALUES:
//		
//		true : notifications for this file are to be included...
//			   notifiy the client by calling the appropriate CDirectoryChangeHandler::On_Filexxx() function.
//		false: this file is not included.... do not notifiy the client...
//
{
	assert( szFileName );

	if( m_szIncludeFilter == NULL ) // no filter specified, all files pass....
		return true;
	if( m_nNumIncludeFilterSpecs == 1 )
	{
		return _PathMatchSpec(szFileName, m_szIncludeFilter)? true : false;
	}
	else
	{
		TCHAR ** ppTmp = (TCHAR**)m_szIncludeFilter;
		for(int i(0); i < m_nNumIncludeFilterSpecs; ++i )
		{
			if( _PathMatchSpec(szFileName, *ppTmp++) )
				return true;
		}
		return false;
	}

	return false;
}

bool CDelayedDirectoryChangeHandler::ExcludeThisNotification(LPCTSTR szFileName)
//
//	Tests the file name to see if it matches a filter specification
//	if this function returns true, it means that this notification
//	is NOT to be passed to the client.... changes to this kind of file
//	are not
//
//	RETURN VALUES:
//		
//		true :   notifications for this file are to be filtered out(EXCLUDED)...
//				 do not notifify the client code.
//		false:   notifications for this file are NOT to be filtered out
//
//
{

	assert( szFileName );

	if( m_szExcludeFilter == NULL ) // no exclude filter... nothing is excluded...
		return false;
	if( m_nNumExcludeFilterSpecs == 1 )
	{
		if( _PathMatchSpec(szFileName, m_szExcludeFilter) )
			return true;//exclude this notification...
		return false;
	}
	else
	{
		TCHAR ** ppTmp = (TCHAR**)m_szExcludeFilter;
		for(int i(0); i < m_nNumExcludeFilterSpecs; ++i )
		{
			if( _PathMatchSpec(szFileName, *ppTmp++) )
				return true;//exclude this one...
		}
		return false;//didn't match any exclude filters...don't exclude it
	}
/**
	if( m_szExcludeFilter == NULL //no exclude filter specified, not excluding anything....
	||	!PathMatchSpec(szFileName, m_szExcludeFilter) )//or didn't match filter pattern.. this is not excluded...
	{
		return false;
	}
	return true;
***/
	
}

void CDelayedDirectoryChangeHandler::DispatchNotificationFunction(CDirChangeNotification * pNotification)
/*****************************************************
	This function is called when we want the notification to execute.

	
******************************************************/
{
	assert( m_pRealHandler );
	assert( pNotification );
	if( pNotification && m_pRealHandler )
	{
		//
		//	Allow the client to ignore the notification
		//
		//
		if( NotifyClientOfFileChange(pNotification))
		{
			switch( pNotification->m_eFunctionToDispatch )
			{
			case CDirChangeNotification::eOn_FileAdded:
				
				m_pRealHandler->On_FileAdded( pNotification->m_szFileName1 ); 
				break;
				
			case CDirChangeNotification::eOn_FileRemoved:
				
				m_pRealHandler->On_FileRemoved( pNotification->m_szFileName1 );
				break;
				
			case CDirChangeNotification::eOn_FileNameChanged:
				
				m_pRealHandler->On_FileNameChanged( pNotification->m_szFileName1, pNotification->m_szFileName2 );
				break;
				
			case CDirChangeNotification::eOn_FileModified:
				
				m_pRealHandler->On_FileModified( pNotification->m_szFileName1 );
				break;
				
			case CDirChangeNotification::eOn_ReadDirectoryChangesError:
				
				m_pRealHandler->On_ReadDirectoryChangesError( pNotification->m_dwError, pNotification->m_szFileName1 );
				break;
				
			case CDirChangeNotification::eOn_WatchStarted:
		
				m_pRealHandler->On_WatchStarted(pNotification->m_dwError, pNotification->m_szFileName1);
				break;
		
			case CDirChangeNotification::eOn_WatchStopped:
				
				try{
					//
					//	The exception handler is just in case of the condition described in DirectoryChanges.h
					//	in the comments for On_WatchStopped()
					//
					m_pRealHandler->On_WatchStopped(pNotification->m_szFileName1);

				}catch(...){
					MessageBeep( 0xffff );
					MessageBeep( 0xffff );
			#ifdef DEBUG
					MessageBox(NULL,_T("An RTFM Exception was raised in On_WatchStopped() -- see Comments for CDirectoryChangeHandler::On_WatchStopped() in DirectoryChanges.h."), _T("Programmer Note(DEBUG INFO):"), MB_ICONEXCLAMATION | MB_OK);
			#endif
				}
				//
				//	Signal that the On_WatchStopped() function has been dispatched.
				//
				if( m_hWatchStoppedDispatchedEvent )
					SetEvent(m_hWatchStoppedDispatchedEvent);
				break;
			case CDirChangeNotification::eFunctionNotDefined:
			default:
				break;
			}//end switch()
		}
	}	
	if( pNotification )						 //		
		DisposeOfNotification(pNotification);// deletes or releases the notification object from memory/use
											 //
}

BOOL CDelayedDirectoryChangeHandler::WaitForOnWatchStoppedDispatched( )
//
//	When shutting down, m_pRealHandler->On_WatchStopped() will be called.
//	Because it's possible that this object will be deleted before the notification
//	can be dispatched to the other thread, we have to wait until we know that it's been executed
//	before returning control.
//
//	This function signals that the function has been dispatched to the other
//	thread and it will be safe to delete this object once this has returned.
//
{
	assert( m_hWatchStoppedDispatchedEvent );
	DWORD dwWait = WAIT_FAILED;
	if( m_hWatchStoppedDispatchedEvent )
	{

		if( m_bAppHasGUI == false )
		{
			//
			//	The function will be dispatched to another thread...
			//	just wait for the event to be signalled....
			do{
				dwWait	= WaitForSingleObject(m_hWatchStoppedDispatchedEvent, 5000);//wait five seconds
				if( dwWait != WAIT_OBJECT_0 )
				{
					TRACE4(_T("WARNING: Possible Deadlock detected! ThreadID: %d File: %s Line: %d\n"), GetCurrentThreadId(), _T(__FILE__), __LINE__);
				}
			}while( dwWait != WAIT_OBJECT_0 );
		}
		else
		{
			//
			//	Note to self:  This thread doesn't have a message Q, and therefore can't attach to 
			//	receive messages and process them... MsgWaitForMultipleObjects won't wake up for messages 
			//	unless i attach myself the the other threads input Q....
			//	just use MsgWaitForMultipleObjects() in place of WaitForSingleObject in the places where it's used...
			//
			do{
				dwWait = MsgWaitForMultipleObjects(1, &m_hWatchStoppedDispatchedEvent, 
												   FALSE, 5000, 
												   QS_ALLEVENTS);//wake up for all events, sent messages, posted messages etc.
				switch(dwWait)
				{
				case WAIT_OBJECT_0:
					{
						//
						// The event has become signalled
						//
						
					}break;
				case WAIT_OBJECT_0 + 1: 
					{
						//
						//	There is a message in this thread's queue, so 
						//	MsgWaitForMultipleObjects returned.
						//	Process those messages, and wait again.

						MSG msg;
						while( PeekMessage(&msg, NULL, 0,0, PM_REMOVE ) ) 
						{
							if( msg.message != WM_QUIT)
							{
								TranslateMessage(&msg);
								DispatchMessage(&msg);
							}
							else
							{
								/****
								NOTE: putting WM_QUIT back in the Q caused problems. forget about it.
								****/
								break;
							}
						}
					}break;
				case WAIT_TIMEOUT:
					{
						TRACE4(_T("WARNING: Possible Deadlock detected! ThreadID: %d File: %s Line: %d\n"), GetCurrentThreadId(), _T(__FILE__), __LINE__);
					}break;
				}
			}while( dwWait != WAIT_OBJECT_0 );
			assert( dwWait == WAIT_OBJECT_0 );
		}

	}
	else
	{
		TRACE1(_T("WARNING: Unable to wait for notification that the On_WatchStopped function has been dispatched to another thread.\n"));
		TRACE1(_T("An Exception may occur shortly.\n"));
		TRACE3(_T("File: %s Line: %d"), _T( __FILE__ ), __LINE__);
	
	}


	return (dwWait == WAIT_OBJECT_0 );
}

void CDelayedDirectoryChangeHandler::SetChangedDirectoryName(const std::wstring & strChangedDirName)
{
	assert( m_pRealHandler );
	CDirectoryChangeHandler::SetChangedDirectoryName(strChangedDirName);
	if( m_pRealHandler )
		m_pRealHandler->SetChangedDirectoryName( strChangedDirName );
}
const std::wstring & CDelayedDirectoryChangeHandler::GetChangedDirectoryName() const
{
	if( m_pRealHandler )
		return m_pRealHandler->GetChangedDirectoryName();
	return CDirectoryChangeHandler::GetChangedDirectoryName();
}