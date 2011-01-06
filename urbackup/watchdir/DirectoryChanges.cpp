// DirectoryChanges.cpp: implementation of the CDirectoryChangeWatcher and CDirectoryChangeHandler classes.
//
///////////////////////////////////////////////////////////////////
///

/***********************************************************

  Author:	Wes Jones wesj@hotmail.com

  File:		DirectoryChanges.cpp

  Latest Changes:

		11/22/2001	--	Fixed bug causing file name's to be truncated if
						longer than 130 characters. Fixed CFileNotifyInformation::GetFileName()
						Thanks to Edric(uo_edric@hotmail.com) for pointing this out.

						Added code to enable process privileges when CDirectoryChangeWatcher::WatchDirectory() 
						is first called.	See docuementation API for ReadDirectoryChangesW() for more information of required privileges.
						
						  Currently enables these privileges: (11/22/2001)
							SE_BACKUP_NAME
							SE_CHANGE_NOTIFY_NAME 
							SE_RESTORE_NAME(02/09/2002)
						Implemented w/ helper class CPrivilegeEnabler.

		11/23/2001		Added classes so that all notifications are handled by the
						same thread that called CDirectoryChangeWatcher::WatchDirectory(),
						ie: the main thread.
						CDirectoryChangeHandler::On_Filexxxx() functions are now called in the 
						context of the main thread instead of the worker thread.

						This is good for two reasons:
						1: The worker thread spends less time handling each notification.
							The worker thread simply passes the notification to the main thread,
							which does the processing.
							This means that each file change notification is handled as fast as possible
							and ReadDirectoryChangesW() can be called again to receive more notifications
							faster.

						2:	This makes it easier to make an ActiveX or ATL object with this class
							because the events that are fired, fire in the context of the main thread.
							The fact that I'm using a worker thread w/ this class is totally 
							transparent to a client user.
							If I decide to turn this app into an ActiveX or ATL object
							I don't have to worry about wierd COM rules and multithreading issues,
							and neither does the client, be the client a VB app, C++ app, Delphi app, or whatever.

						Implemented with CDelayedDirectoryChangeHandler in DirectoryChangeHandler.h/.cpp

		02/06/2002	  Fixed a bug that would cause an application to hang.
						If ReadDirectoryChangesW was to fail during normal operation,
						the short story is that the application would hang
						when it called CDirectoryChangeWatcher::UnwatchDirectory(const CString & )

						One way to reproduce this behavior on the old code
						is to watch a directory using a UNC path, and then change the IP
						address of that machine while the watch was running.  Exitting 
						the app after this would cause it to hang.

						Steps to reproduce it:

						1) Assume that the computer running the code is
							named 'ThisComputer' and there is a shared folder named 'FolderName'


						2) Start a watch on a folder using a UNC path:  ie: \\ThisComputer\FolderName
						
						  eg:	CDirectoryChangeWatcher	watcher;
							 
								watcher.WatchDirectory(_T("\\\\ThisComputer\\FolderName",/ * other parameters * /)
							
						3)  Change the IP address of 'ThisComputer'

							   ** ReadDirectoryChangesW() will fail some point after this.
						
		
						4)  Exit the application... it may hang.


						Anyways, that's what the bug fix is for.


		02/06/2002	New side effects for CDirectoryChangeHandler::On_ReadDirectoryChangeError()

					If CDirectoryChangeHandler::On_ReadDirectoryChangeError() is ever executed
					the directory that you are watching will have been unwatched automatically due
					to the error condition.

					A call to CDirectoryChangeWatcher::IsWatchingDirectory() will fail because the directory
					is no longer being watched. You'll need to re-watch that directory.

		02/09/2002	Added a parameter to CDirectoryChangeHandler::On_ReadDirectoryChangeError()

					Added the parameter: const CString & strDirectoryName
					The new signature is now:
							virtual void CDirectoryChangeHandler::On_ReadDirectoryChangeError(DWORD dwError, const CString & strDirectoryName);

					This new parameter gives you the name of the directory that the error occurred on.

		04/25/2002  Provided a way to get around the requirement of a message pump.
					A console app can now use this w/out problems by passing false
					to the constructor of CDirectoryChangeWatcher. 
					An app w/ a message pump can also pass false if it so desires w/out problems.

		04/25/2002	Added two new virtual functions to CDirectoryChangeHandler

					Added:
							On_WatchStarted(DWORD dwError, const CString & strDirectoryName)
							On_WatchStopped(const CString & strDirectoryName);
					See header file for details.

		04/27/2002	Added new function to CDirectoryChangeHandler:

					Added virtual bool On_FilterNotification(DWORD dwNotifyAction, LPCTSTR szFileName, LPCTSTR szNewFileName)

					This function is called before any notification function, and allows the 
					CDirectoryChangeHandler derived class to ignore file notifications 
					by performing a programmer defined test.
					By ignore, i mean that 
						On_FileAdded(), On_FileRemoved(), On_FileModified(), or On_FileNameChanged()
						will NOT be called if this function returns false.

					The default implementation always returns true, signifying that ALL notifications
					are to be called.


		04/27/2002  Added new Parameters to CDirectoryChangeWatcher::WatchDirectory()

					The new parameters are: 
										LPCTSTR szIncludeFilter 
										LPCTSTR szExcludeFilter
					Both parameters are defaulted to NULL.
					Signature is now:
					CDirectoryChangeWatcher::WatchDirectory(const CString & strDirToWatch, 
															DWORD dwChangesToWatchFor, 
															CDirectoryChangeHandler * pChangeHandler, 
															BOOL bWatchSubDirs = FALSE,
															LPCTSTR szIncludeFilter = NULL,
															LPCTSTR szExcludeFilter = NULL)

		04/27/2002	Added support for include and exclude filters.
					These filters allow you to receive notifications for just the files you
					want... ie: you can specify that you only want to receive notifications
					for changes to "*.txt" files or some other such file filter.

					NOTE: This feature is implemented w/ the PathMatchSpec() api function
					which is only available on NT4.0 if Internet Explorer 4.0 or above is installed.
					See MSDN for PathMatchSpec().  Win2000, and XP do not have to worry about it.

					Filter specifications:
					   Accepts wild card characters * and ?, just as you're used to for the DOS dir command.
					   It is possible to specify multiple extenstions in the filter by separating each filter spec
					   with a semi-colon. 
					   eg: "*.txt;*.tmp;*.log"  <-- this filter specifies all .txt, .tmp, & .log files

					

					Filters are passed as parameters to CDirectoryChangeWatcher::WatchDirectory()

					NOTE: The filters you specify take precedence over CDirectoryChangeHandler::On_FilterNotification().
						  This means that if the file name does not pass the filters that you specify
						  when the watch is started, On_FilterNotification() will not be called.
						  Filter specifications are case insensitive, ie: ".tmp" and ".TMP" are the same


					FILTER TYPES:
							Include Filter:
										If you specify an include filter, you are specifying that you
										only want to receive notifications for specific file types.
										eg: "*.log" means that you only want notifications for changes 
											to files w/ an exention of ".log". 
											Changes to ALL OTHER other file types are ignored.
										An empty, or not specified include filter means that you want
										notifications for changes of ALL file types.
							Exclude filter:

										If you specify an exclude filter, you are specifying that
										you do not wish to receive notifications for a specific type of file or files.
										eg: "*.tmp" would mean that you do not want any notifications 
											regarding files that end w/ ".tmp"
										An empty, or not specified exclude filter means that
										you do not wish to exclude any file notifications.





		
					
************************************************************/
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <process.h>
#include "DirectoryChanges.h"
#include "DelayedDirectoryChangeHandler.h"
#include "../../stringtools.h"
//
//
//	Fwd Declares & #define's
//
//
//
// Helper classes
class CPrivilegeEnabler;	 //for use w/ enabling process priveledges when this code is first used. It's a singleton.

class CFileNotifyInformation;//helps CDirectoryChangeWatcher class notify CDirectoryChangeHandler class of changes to files. 

class CDelayedDirectoryChangeHandler;	//	Helps all notifications become handled by the main
										//	thread, or a worker thread depending upon a value passed to the 
										//	constructor of CDirectoryChangeWatcher.
										//	
										//	For notifications to fire in the main thread, your app must have a message pump.
										//
										//  The 'main' thread is the one that called CDirectoryChangeWatcher::WatchDirectory()

// Helper functions
static BOOL	EnablePrivilege(LPCTSTR pszPrivName, BOOL fEnable = TRUE);
static bool IsDirectory(const std::wstring & strPath);
/////////////////////////////////////////////////////////////////////
//	Helper functions.
BOOL	EnablePrivilege(LPCTSTR pszPrivName, BOOL fEnable /*= TRUE*/) 
//
//	I think this code is from a Jeffrey Richter book...
//
//	Enables user priviledges to be set for this process.
//	
//	Process needs to have access to certain priviledges in order
//	to use the ReadDirectoryChangesW() API.  See documentation.
{    
	BOOL fOk = FALSE;    
	// Assume function fails    
	HANDLE hToken;    
	// Try to open this process's access token    
	if (OpenProcessToken(GetCurrentProcess(), 		
					TOKEN_ADJUST_PRIVILEGES, &hToken)) 	
	{        
		// privilege        
		TOKEN_PRIVILEGES tp = { 1 };        

		if( LookupPrivilegeValue(NULL, pszPrivName,  &tp.Privileges[0].Luid) )
		{
			tp.Privileges[0].Attributes = fEnable ?  SE_PRIVILEGE_ENABLED : 0;

			AdjustTokenPrivileges(hToken, FALSE, &tp, 			      
									sizeof(tp), NULL, NULL);

			fOk = (GetLastError() == ERROR_SUCCESS);		
		}
		CloseHandle(hToken);	
	}	
	return(fOk);
}

static bool IsDirectory(const std::wstring & strPath)
//
//  Returns: bool
//		true if strPath is a path to a directory
//		false otherwise.
//
{
	DWORD dwAttrib	= GetFileAttributesW( strPath.c_str() );
	return static_cast<bool>( ( dwAttrib != 0xffffffff 
							&&	(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) );

		
}
///////////////////////////////////////////////
//Helper class:

class CFileNotifyInformation 
/*******************************

A Class to more easily traverse the FILE_NOTIFY_INFORMATION records returned 
by ReadDirectoryChangesW().

FILE_NOTIFY_INFORMATION is defined in Winnt.h as: 

 typedef struct _FILE_NOTIFY_INFORMATION {
    DWORD NextEntryOffset;
	DWORD Action;
    DWORD FileNameLength;
    WCHAR FileName[1];
} FILE_NOTIFY_INFORMATION, *PFILE_NOTIFY_INFORMATION;	

  ReadDirectoryChangesW basically puts x amount of these records in a 
  buffer that you specify.
  The FILE_NOTIFY_INFORMATION structure is a 'dynamically sized' structure (size depends on length
  of the file name (+ sizeof the DWORDs in the struct))

  Because each structure contains an offset to the 'next' file notification
  it is basically a singly linked list.  This class treats the structure in that way.
  

  Sample Usage:
  BYTE Read_Buffer[ 4096 ];

  ...
  ReadDirectoryChangesW(...Read_Buffer, 4096,...);
  ...

  CFileNotifyInformation notify_info( Read_Buffer, 4096);
  do{
	    switch( notify_info.GetAction() )
		{
		case xx:
		    notify_info.GetFileName();
		}

  while( notify_info.GetNextNotifyInformation() );
  
********************************/
{
public:
	CFileNotifyInformation( BYTE * lpFileNotifyInfoBuffer, DWORD dwBuffSize)
	: m_pBuffer( lpFileNotifyInfoBuffer ),
	  m_dwBufferSize( dwBuffSize )
	{
		assert( lpFileNotifyInfoBuffer && dwBuffSize );
		
		m_pCurrentRecord = (PFILE_NOTIFY_INFORMATION) m_pBuffer;
	}

	
	BOOL GetNextNotifyInformation();
	
	BOOL CopyCurrentRecordToBeginningOfBuffer(OUT DWORD & ref_dwSizeOfCurrentRecord);

	DWORD	GetAction() const;//gets the type of file change notifiation
	std::wstring GetFileName()const;//gets the file name from the FILE_NOTIFY_INFORMATION record
	std::wstring GetFileNameWithPath(const std::wstring & strRootPath) const;//same as GetFileName() only it prefixes the strRootPath into the file name

	
protected:
	BYTE * m_pBuffer;//<--all of the FILE_NOTIFY_INFORMATION records 'live' in the buffer this points to...
	DWORD  m_dwBufferSize;
	PFILE_NOTIFY_INFORMATION m_pCurrentRecord;//this points to the current FILE_NOTIFY_INFORMATION record in m_pBuffer
	
};

BOOL CFileNotifyInformation::GetNextNotifyInformation()
/***************
  Sets the m_pCurrentRecord to the next FILE_NOTIFY_INFORMATION record.

  Even if this return FALSE, (unless m_pCurrentRecord is NULL)
  m_pCurrentRecord will still point to the last record in the buffer.
****************/
{
	if( m_pCurrentRecord 
	&&	m_pCurrentRecord->NextEntryOffset != 0UL)//is there another record after this one?
	{
		//set the current record to point to the 'next' record
		PFILE_NOTIFY_INFORMATION pOld = m_pCurrentRecord;
		m_pCurrentRecord = (PFILE_NOTIFY_INFORMATION) ((LPBYTE)m_pCurrentRecord + m_pCurrentRecord->NextEntryOffset);

		assert( (DWORD)((BYTE*)m_pCurrentRecord - m_pBuffer) < m_dwBufferSize);//make sure we haven't gone too far

		if( (DWORD)((BYTE*)m_pCurrentRecord - m_pBuffer) > m_dwBufferSize )
		{
			//we've gone too far.... this data is hosed.
			//
			// This sometimes happens if the watched directory becomes deleted... remove the FILE_SHARE_DELETE flag when using CreateFile() to get the handle to the directory...
			m_pCurrentRecord = pOld;
		}
					
		return (BOOL)(m_pCurrentRecord != pOld);
	}
	return FALSE;
}

BOOL CFileNotifyInformation::CopyCurrentRecordToBeginningOfBuffer(OUT DWORD & ref_dwSizeOfCurrentRecord)
/*****************************************
   Copies the FILE_NOTIFY_INFORMATION record to the beginning of the buffer
   specified in the constructor.

   The size of the current record is returned in DWORD & dwSizeOfCurrentRecord.
   
*****************************************/
{
	assert( m_pBuffer && m_pCurrentRecord );
	if( !m_pCurrentRecord ) return FALSE;

	BOOL bRetVal = TRUE;

	//determine the size of the current record.
	ref_dwSizeOfCurrentRecord = sizeof( FILE_NOTIFY_INFORMATION );
	//subtract out sizeof FILE_NOTIFY_INFORMATION::FileName[1]
	WCHAR FileName[1];//same as is defined for FILE_NOTIFY_INFORMATION::FileName
	UNREFERENCED_PARAMETER(FileName);
	ref_dwSizeOfCurrentRecord -= sizeof(FileName);   
	//and replace it w/ value of FILE_NOTIFY_INFORMATION::FileNameLength
	ref_dwSizeOfCurrentRecord += m_pCurrentRecord->FileNameLength;

	assert( (DWORD)((LPBYTE)m_pCurrentRecord + ref_dwSizeOfCurrentRecord) <= m_dwBufferSize );

	assert( (void*)m_pBuffer != (void*)m_pCurrentRecord );//if this is the case, your buffer is way too small
	if( (void*)m_pBuffer != (void*) m_pCurrentRecord )
	{//copy the m_pCurrentRecord to the beginning of m_pBuffer
		
		assert( (DWORD)m_pCurrentRecord > (DWORD)m_pBuffer + ref_dwSizeOfCurrentRecord);//will it overlap?
		__try{
			memcpy(m_pBuffer, m_pCurrentRecord, ref_dwSizeOfCurrentRecord);
			bRetVal = TRUE;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			//TRACE(_T("EXCEPTION!  CFileNotifyInformation::CopyCurrentRecordToBeginningOfBuffer() -- probably because bytes overlapped in a call to memcpy()"));
			bRetVal = FALSE;
		}
	}
	//else
	//there was only one record in this buffer, and m_pCurrentRecord is already at the beginning of the buffer
	return bRetVal;
}

DWORD CFileNotifyInformation::GetAction() const
{ 
	assert( m_pCurrentRecord );
	if( m_pCurrentRecord )
		return m_pCurrentRecord->Action;
	return 0UL;
}

std::wstring CFileNotifyInformation::GetFileName() const
{
	//
	//BUG FIX:
	//		File Name's longer than 130 characters are truncated
	//
	//		Thanks Edric @ uo_edric@hotmail.com for pointing this out.
	if( m_pCurrentRecord )
	{
		WCHAR wcFileName[ MAX_PATH + 1] = {0};//L"";
		memcpy(	wcFileName, 
				m_pCurrentRecord->FileName, 
				//min( MAX_PATH, m_pCurrentRecord->FileNameLength) <-- buggy line
				(std::min)( (int)(MAX_PATH * sizeof(WCHAR)), (int)m_pCurrentRecord->FileNameLength));
		

		return std::wstring( wcFileName );
	}
	return std::wstring(L"");
}		

static inline bool HasTrailingBackslash(const std::wstring & str )
{
	if( str.size() > 0
	&&	str[ str.size() - 1 ] == '\\' )
		return true;
	return false;
}
std::wstring CFileNotifyInformation::GetFileNameWithPath(const std::wstring & strRootPath) const
{
	std::wstring strFileName( strRootPath );
	//if( strFileName.Right(1) != _T("\\") )
	if( !HasTrailingBackslash( strRootPath ) )
		strFileName += L"\\";

	strFileName += GetFileName();
	return strFileName;
}
/////////////////////////////////////////////////////////////////////////////////
class CPrivilegeEnabler
//
//	Enables privileges for this process
//	first time CDirectoryChangeWatcher::WatchDirectory() is called.
//
//	It's a singleton.
//
{
private:
	CPrivilegeEnabler();//ctor
public:
	~CPrivilegeEnabler(){};
	
	static CPrivilegeEnabler & Instance();
	//friend static CPrivilegeEnabler & Instance();
};

CPrivilegeEnabler::CPrivilegeEnabler()
{
	LPCTSTR arPrivelegeNames[]	=	{
										SE_BACKUP_NAME, //	these two are required for the FILE_FLAG_BACKUP_SEMANTICS flag used in the call to 
										SE_RESTORE_NAME,//  CreateFile() to open the directory handle for ReadDirectoryChangesW

										SE_CHANGE_NOTIFY_NAME //just to make sure...it's on by default for all users.
										//<others here as needed>
									};
	for(int i = 0; i < sizeof(arPrivelegeNames) / sizeof(arPrivelegeNames[0]); ++i)
	{
		if( !EnablePrivilege(arPrivelegeNames[i], TRUE) )
		{
			/*TRACE(_T("Unable to enable privilege: %s	--	GetLastError(): %d\n"), arPrivelegeNames[i], GetLastError());
			TRACE(_T("CDirectoryChangeWatcher notifications may not work as intended due to insufficient access rights/process privileges.\n"));
			TRACE(_T("File: %s Line: %d\n"), _T(__FILE__), __LINE__);*/
		}
	}
}

CPrivilegeEnabler & CPrivilegeEnabler::Instance()
{
	static CPrivilegeEnabler theInstance;//constructs this first time it's called.
	return theInstance;
}
//
//
//
///////////////////////////////////////////////////////////

	
//
//
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CDirectoryChangeHandler::CDirectoryChangeHandler()
: m_nRefCnt( 1 ),
  m_pDirChangeWatcher( NULL ),
  m_nWatcherRefCnt( 0L )
{
}

CDirectoryChangeHandler::~CDirectoryChangeHandler()
{
	UnwatchDirectory();
}

long CDirectoryChangeHandler::AddRef()
{ 
	return InterlockedIncrement(&m_nRefCnt);	
}

long CDirectoryChangeHandler::Release()
{  
	long nRefCnt = -1;
	if( (nRefCnt = InterlockedDecrement(&m_nRefCnt)) == 0 )
		delete this;
	return nRefCnt;
}
long CDirectoryChangeHandler::CurRefCnt()const 
{ 
	return m_nRefCnt;
}

BOOL CDirectoryChangeHandler::UnwatchDirectory()
{
	CSingleLock lock(&m_csWatcher, TRUE);

	if( m_pDirChangeWatcher )
		return m_pDirChangeWatcher->UnwatchDirectory( this );
	return FALSE;
}

long  CDirectoryChangeHandler::ReferencesWatcher(CDirectoryChangeWatcher * pDirChangeWatcher)
{
	assert( pDirChangeWatcher );
	CSingleLock lock(&m_csWatcher, TRUE);
	if( m_pDirChangeWatcher 
	&&  m_pDirChangeWatcher != pDirChangeWatcher )
	{
		/*TRACE(_T("CDirectoryChangeHandler...is becoming used by a different CDirectoryChangeWatcher!\n"));
		TRACE(_T("Directories being handled by this object will now be unwatched.\nThis object is now being used to ")
			  _T("handle changes to a directory watched by different CDirectoryChangeWatcher object, probably on a different directory"));*/
		
		if( UnwatchDirectory() )
		{
			m_pDirChangeWatcher = pDirChangeWatcher;
			m_nWatcherRefCnt = 1; //when this reaches 0, set m_pDirChangeWatcher to NULL
			return m_nWatcherRefCnt;
		}
		else
		{
			assert( FALSE );//shouldn't get here!
		}
	}
	else
	{
		assert( !m_pDirChangeWatcher || m_pDirChangeWatcher == pDirChangeWatcher );
		
		m_pDirChangeWatcher = pDirChangeWatcher;	
		
		if( m_pDirChangeWatcher )
			return InterlockedIncrement(&m_nWatcherRefCnt);
		
	}
	return m_nWatcherRefCnt;
}

long CDirectoryChangeHandler::ReleaseReferenceToWatcher(CDirectoryChangeWatcher * pDirChangeWatcher)
{
	assert( m_pDirChangeWatcher == pDirChangeWatcher );
	CSingleLock lock(&m_csWatcher, TRUE);
	long nRef;
	if( (nRef = InterlockedDecrement(&m_nWatcherRefCnt)) <= 0L )
	{
		m_pDirChangeWatcher = NULL; //Setting this to NULL so that this->UnwatchDirectory() which is called in the dtor
									//won't call m_pDirChangeWatcher->UnwatchDirecotry(this).
									//m_pDirChangeWatcher may point to a destructed object depending on how
									//these classes are being used.
		m_nWatcherRefCnt = 0L;
	}
	return nRef;
}

//
//
//	Default implmentations for CDirectoryChangeHandler's virtual functions.
//
//
void CDirectoryChangeHandler::On_FileAdded(const std::wstring & strFileName)
{ 
	//TRACE(_T("The following file was added: %s\n"), strFileName);
}

void CDirectoryChangeHandler::On_FileRemoved(const std::wstring & strFileName)
{
	TRACE(_T("The following file was removed: %s\n"), strFileName);
}

void CDirectoryChangeHandler::On_FileModified(const std::wstring & strFileName)
{
	TRACE(_T("The following file was modified: %s\n"), strFileName);
}

void CDirectoryChangeHandler::On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName)
{
	TRACE3(_T("The file %s was RENAMED to %s\n"), strOldFileName, strNewFileName);
}
void CDirectoryChangeHandler::On_ReadDirectoryChangesError(DWORD dwError, const std::wstring & strDirectoryName)
{
	TRACE1(_T("WARNING!!!!!\n") );
	TRACE1(_T("An error has occurred on a watched directory!\n"));
	TRACE(_T("This directory has become unwatched! -- %s \n"), strDirectoryName);
	TRACE(_T("ReadDirectoryChangesW has failed! %d"), dwError);
	assert( FALSE );//you should override this function no matter what. an error will occur someday.
}

void CDirectoryChangeHandler::On_WatchStarted(DWORD dwError, const std::wstring & strDirectoryName)
{	
	if( dwError == 0 )
		TRACE(_T("A watch has begun on the following directory: %s\n"), strDirectoryName);
	else
		TRACE3(_T("A watch failed to start on the following directory: (Error: %d) %s\n"),dwError, strDirectoryName);
}

void CDirectoryChangeHandler::On_WatchStopped(const std::wstring & strDirectoryName)
{
	TRACE(_T("The watch on the following directory has stopped: %s\n"), strDirectoryName);
}

bool CDirectoryChangeHandler::On_FilterNotification(DWORD /*dwNotifyAction*/, LPCTSTR /*szFileName*/, LPCTSTR /*szNewFileName*/)
//
//	bool On_FilterNotification(DWORD dwNotifyAction, LPCTSTR szFileName, LPCTSTR szNewFileName);
//
//	This function gives your class a chance to filter unwanted notifications.
//
//	PARAMETERS: 
//			DWORD	dwNotifyAction	-- specifies the event to filter
//			LPCTSTR szFileName		-- specifies the name of the file for the event.
//			LPCTSTR szNewFileName	-- specifies the new file name of a file that has been renamed.
//
//	RETURN VALUE:
//			return true from this function, and you will receive the notification.
//			return false from this function, and your class will NOT receive the notification.
//
//	Valid values of dwNotifyAction:
//		FILE_ACTION_ADDED			-- On_FileAdded() is about to be called.
//		FILE_ACTION_REMOVED			-- On_FileRemoved() is about to be called.
//		FILE_ACTION_MODIFIED		-- On_FileModified() is about to be called.
//		FILE_ACTION_RENAMED_OLD_NAME-- On_FileNameChanged() is about to be call.
//
//	  
//	NOTE:  When the value of dwNotifyAction is FILE_ACTION_RENAMED_OLD_NAME,
//			szFileName will be the old name of the file, and szNewFileName will
//			be the new name of the renamed file.
//
//  The default implementation always returns true, indicating that all notifications will 
//	be sent.
//	
{
	return true;
}

void CDirectoryChangeHandler::SetChangedDirectoryName(const std::wstring & strChangedDirName)
{
	m_strChangedDirectoryName = strChangedDirName;
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

CDirectoryChangeWatcher::CDirectoryChangeWatcher(bool bAppHasGUI /*= true*/, DWORD dwFilterFlags/*=FILTERS_CHECK_FILE_NAME_ONLY*/)
: m_hCompPort( NULL )
 ,m_hThread( NULL )
 ,m_bAppHasGUI( bAppHasGUI )
 ,m_dwFilterFlags( dwFilterFlags == 0? FILTERS_DEFAULT_BEHAVIOR : dwFilterFlags)
{
	//NOTE:  
	//	The bAppHasGUI variable indicates that you have a message pump associated
	//	with the main thread(or the thread that first calls CDirectoryChangeWatcher::WatchDirectory() ).
	//	Directory change notifications are dispatched to your main thread.
	//	
	//	If your app doesn't have a gui, then pass false.  Doing so causes a worker thread
	//	to be created that implements a message pump where it dispatches/executes the notifications.
	//  It's ok to pass false even if your app does have a GUI.
	//	Passing false is required for Console applications, or applications without a message pump.
	//	Note that notifications are fired in a worker thread.
	//

	//NOTE:
	//
	//
}

CDirectoryChangeWatcher::~CDirectoryChangeWatcher()
{

	UnwatchAllDirectories();

	if( m_hCompPort )
	{
		CloseHandle( m_hCompPort );
		m_hCompPort = NULL;
	}
}

DWORD CDirectoryChangeWatcher::SetFilterFlags(DWORD dwFilterFlags)
//
//	SetFilterFlags()
//	
//	sets filter behavior for directories watched AFTER this function has been called.
//
//
//
{
	DWORD dwOld = m_dwFilterFlags;
	m_dwFilterFlags = dwFilterFlags;
	if( m_dwFilterFlags == 0 )
		m_dwFilterFlags = FILTERS_DEFAULT_BEHAVIOR;//the default.
	return dwOld;
}

BOOL CDirectoryChangeWatcher::IsWatchingDirectory(const std::wstring & strDirName)const
/*********************************************
  Determines whether or not a directory is being watched

  be carefull that you have the same name path name, including the backslash
  as was used in the call to WatchDirectory().

	  ie:	
			"C:\\Temp"
		is different than
			"C:\\Temp\\"
**********************************************/
{
	CSingleLock lock( const_cast<CCriticalSection*>(&m_csDirWatchInfo), TRUE);
	int i;
	if( GetDirWatchInfo(strDirName, i) )
		return TRUE;
	return FALSE;
}

int	CDirectoryChangeWatcher::NumWatchedDirectories()const
{
	CSingleLock lock(const_cast<CCriticalSection*>(&m_csDirWatchInfo), TRUE);
	int nCnt(0),max = (int)m_DirectoriesToWatch.size();
	for(int i(0); i < max; ++i)
	{
		if( m_DirectoriesToWatch[i] != NULL )//array may contain NULL elements.
			nCnt++;
	}

	return nCnt;
}

DWORD CDirectoryChangeWatcher::WatchDirectory(const std::wstring & strDirToWatch, 
									   DWORD dwChangesToWatchFor, 
									   CDirectoryChangeHandler * pChangeHandler,
									   BOOL bWatchSubDirs /*=FALSE*/,
									   LPCTSTR szIncludeFilter /*=NULL*/,
									   LPCTSTR szExcludeFilter /*=NULL*/
									   )
/*************************************************************
FUNCTION:	WatchDirectory(const CString & strDirToWatch,   --the name of the directory to watch
						   DWORD dwChangesToWatchFor, --the changes to watch for see dsk docs..for ReadDirectoryChangesW
						   CDirectoryChangeHandler * pChangeHandler -- handles changes in specified directory
						   BOOL bWatchSubDirs      --specified to watch sub directories of the directory that you want to watch
						   )

PARAMETERS:
		const CString & strDirToWatch -- specifies the path of the directory to watch.
		DWORD dwChangesToWatchFor	-- specifies flags to be passed to ReadDirectoryChangesW()
		CDirectoryChangeHandler *	-- ptr to an object which will handle notifications of file changes.
		BOOL bWatchSubDirs			-- specifies to watch subdirectories.
		LPCTSTR szIncludeFilter		-- A file pattern string for files that you wish to receive notifications
									   for. See Remarks.
		LPCTSTR szExcludeFilter		-- A file pattern string for files that you do not wish to receive notifications for. See Remarks

	Starts watching the specified directory(and optionally subdirectories) for the specified changes

	When specified changes take place the appropriate CDirectoryChangeHandler::On_Filexxx() function is called.

	dwChangesToWatchFor can be a combination of the following flags, and changes map out to the 
	following functions:
	FILE_NOTIFY_CHANGE_FILE_NAME    -- CDirectoryChangeHandler::On_FileAdded()
									   CDirectoryChangeHandler::On_FileNameChanged, 
									   CDirectoryChangeHandler::On_FileRemoved
	FILE_NOTIFY_CHANGE_DIR_NAME     -- CDirectoryChangeHandler::On_FileNameAdded(), 
									   CDirectoryChangeHandler::On_FileRemoved
	FILE_NOTIFY_CHANGE_ATTRIBUTES   -- CDirectoryChangeHandler::On_FileModified
	FILE_NOTIFY_CHANGE_SIZE         -- CDirectoryChangeHandler::On_FileModified
	FILE_NOTIFY_CHANGE_LAST_WRITE   -- CDirectoryChangeHandler::On_FileModified
	FILE_NOTIFY_CHANGE_LAST_ACCESS  -- CDirectoryChangeHandler::On_FileModified
	FILE_NOTIFY_CHANGE_CREATION     -- CDirectoryChangeHandler::On_FileModified
	FILE_NOTIFY_CHANGE_SECURITY     -- CDirectoryChangeHandler::On_FileModified?
	

	Returns ERROR_SUCCESS if the directory will be watched, 
	or a windows error code if the directory couldn't be watched.
	The error code will most likely be related to a call to CreateFile(), or 
	from the initial call to ReadDirectoryChangesW().  It's also possible to get an
	error code related to being unable to create an io completion port or being unable 
	to start the worker thread.

	This function will fail if the directory to be watched resides on a 
	computer that is not a Windows NT/2000/XP machine.


	You can only have one watch specified at a time for any particular directory.
	Calling this function with the same directory name will cause the directory to be 
	unwatched, and then watched again(w/ the new parameters that have been passed in).  

**************************************************************/
{
	assert( dwChangesToWatchFor != 0);

	if( strDirToWatch.empty()
	||  dwChangesToWatchFor == 0 
	||  pChangeHandler == NULL )
	{
		TRACE1(_T("ERROR: You've passed invalid parameters to CDirectoryChangeWatcher::WatchDirectory()\n"));
		::SetLastError(ERROR_INVALID_PARAMETER);
		return ERROR_INVALID_PARAMETER;
	}

	
	//double check that it's really a directory
	if( !IsDirectory( strDirToWatch ) )
	{
		TRACE(_T("ERROR: CDirectoryChangeWatcher::WatchDirectory() -- %s is not a directory!\n"), strDirToWatch);
		::SetLastError(ERROR_BAD_PATHNAME);
		return ERROR_BAD_PATHNAME;
	}

	//double check that this directory is not already being watched....
	//if it is, then unwatch it before restarting it...
	if( IsWatchingDirectory( strDirToWatch) )
	{
                UnwatchDirectory( strDirToWatch );

	}
	//
	//
	//	Reference this singleton so that privileges for this process are enabled 
	//	so that it has required permissions to use the ReadDirectoryChangesW API, etc.
	//
	CPrivilegeEnabler::Instance();
	//
	//open the directory to watch....
	HANDLE hDir = CreateFileW(strDirToWatch.c_str(), 
								FILE_LIST_DIRECTORY, 
								FILE_SHARE_READ | FILE_SHARE_WRITE ,//| FILE_SHARE_DELETE, <-- removing FILE_SHARE_DELETE prevents the user or someone else from renaming or deleting the watched directory. This is a good thing to prevent.
								NULL, //security attributes
								OPEN_EXISTING,
								FILE_FLAG_BACKUP_SEMANTICS | //<- the required priviliges for this flag are: SE_BACKUP_NAME and SE_RESTORE_NAME.  CPrivilegeEnabler takes care of that.
                                FILE_FLAG_OVERLAPPED, //OVERLAPPED!
								NULL);
	if( hDir == INVALID_HANDLE_VALUE )
	{
		DWORD dwError = GetLastError();
		TRACE(_T("CDirectoryChangeWatcher::WatchDirectory() -- Couldn't open directory for monitoring. %d\n"), dwError);
		::SetLastError(dwError);//who knows if TRACE will cause GetLastError() to return success...probably won't, but set it manually just for fun.
		return dwError;
	}
	//opened the dir!
	
	CDirWatchInfo * pDirInfo = new CDirWatchInfo( hDir, strDirToWatch, pChangeHandler, dwChangesToWatchFor, bWatchSubDirs, m_bAppHasGUI, szIncludeFilter, szExcludeFilter, m_dwFilterFlags);
	if( !pDirInfo )
	{
		TRACE3(_T("WARNING: Couldn't allocate a new CDirWatchInfo() object --- File: %s Line: %d\n"), _T( __FILE__ ), __LINE__);
		CloseHandle( hDir );
		::SetLastError(ERROR_OUTOFMEMORY);
		return ERROR_OUTOFMEMORY;
	}
	
	//create a IO completion port/or associate this key with
	//the existing IO completion port
	m_hCompPort = CreateIoCompletionPort(hDir, 
										m_hCompPort, //if m_hCompPort is NULL, hDir is associated with a NEW completion port,
													 //if m_hCompPort is NON-NULL, hDir is associated with the existing completion port that the handle m_hCompPort references
										(DWORD)pDirInfo, //the completion 'key'... this ptr is returned from GetQueuedCompletionStatus() when one of the events in the dwChangesToWatchFor filter takes place
										0);
	if( m_hCompPort == NULL )
	{
		TRACE4(_T("ERROR -- Unable to create I/O Completion port! GetLastError(): %d File: %s Line: %d"), GetLastError(), _T( __FILE__ ), __LINE__ );
		DWORD dwError = GetLastError();
		pDirInfo->DeleteSelf( NULL );
		::SetLastError(dwError);//who knows what the last error will be after i call pDirInfo->DeleteSelf(), so set it just to make sure
		return dwError;
	}
	else
	{//completion port created/directory associated w/ it successfully

		//if the thread isn't running start it....
		//when the thread starts, it will call ReadDirectoryChangesW and wait 
		//for changes to take place
		if( m_hThread == NULL )
		{
			//start the thread
			//CWinThread * pThread = AfxBeginThread(MonitorDirectoryChanges, this);
                        m_hThread=(HANDLE)_beginthread(MonitorDirectoryChanges, 0, this);
			if( m_hThread==(void*)-1L )
			{//couldn't create the thread!
				TRACE1(_T("CDirectoryChangeWatcher::WatchDirectory()-- AfxBeginThread failed!\n"));
				pDirInfo->DeleteSelf( NULL );
				return (GetLastError() == ERROR_SUCCESS)? ERROR_MAX_THRDS_REACHED : GetLastError();
			}
		}
		if( m_hThread != NULL )
		{//thread is running, 
			//signal the thread to issue the initial call to
			//ReadDirectoryChangesW()
		   DWORD dwStarted = pDirInfo->StartMonitor( m_hCompPort );

		   if( dwStarted != ERROR_SUCCESS )
		   {//there was a problem!
			   TRACE(_T("Unable to watch directory: %s -- GetLastError(): %d\n"), dwStarted);
			   pDirInfo->DeleteSelf( NULL );
				::SetLastError(dwStarted);//I think this'll set the Err object in a VB app.....
			   return dwStarted;
		   }
		   else
		   {//ReadDirectoryChangesW was successfull!
				//add the directory info to the first empty slot in the array

				//associate the pChangeHandler with this object
				pChangeHandler->ReferencesWatcher( this );//reference is removed when directory is unwatched.
				//CDirWatchInfo::DeleteSelf() must now be called w/ this CDirectoryChangeWatcher pointer becuse
				//of a reference count

				//save the CDirWatchInfo* so I'll be able to use it later.
				AddToWatchInfo( pDirInfo );
				SetLastError(dwStarted);
				return dwStarted;
		   }

		}
		else
		{
			assert(FALSE);//control path shouldn't get here
			::SetLastError(ERROR_MAX_THRDS_REACHED);
			return ERROR_MAX_THRDS_REACHED;
		}
		
	}
	assert( FALSE );//shouldn't get here.
        return 0;
}

BOOL CDirectoryChangeWatcher::UnwatchAllDirectories()
{
	
	//unwatch all of the watched directories
	//delete all of the CDirWatchInfo objects,
	//kill off the worker thread
	if( m_hThread != NULL )
	{
		assert( m_hCompPort != NULL );
		
		CSingleLock lock( &m_csDirWatchInfo, TRUE);

		CDirWatchInfo * pDirInfo;
		//Unwatch each of the watched directories
		//and delete the CDirWatchInfo associated w/ that directory...
		int max = (int)m_DirectoriesToWatch.size();
		for(int i = 0; i < max; ++i)
		{
			if( (pDirInfo = m_DirectoriesToWatch[i]) != NULL )
			{
				 pDirInfo->UnwatchDirectory( m_hCompPort ) ;

				m_DirectoriesToWatch[i]=NULL;
				pDirInfo->DeleteSelf(this);
			}
			
		}
		m_DirectoriesToWatch.clear();
		//kill off the thread
		PostQueuedCompletionStatus(m_hCompPort, 0, 0, NULL);//The thread will catch this and exit the thread
		//wait for it to exit
		WaitForSingleObject(m_hThread, INFINITE);
		//CloseHandle( m_hThread );//Since thread was started w/ AfxBeginThread() this handle is closed automatically, closing it again will raise an exception
		m_hThread = NULL;

		//close the completion port...
		CloseHandle( m_hCompPort );
		m_hCompPort = NULL;


		return TRUE;
	}
	else
	{
#ifdef _DEBUG
		//make sure that there aren't any 
		//CDirWatchInfo objects laying around... they should have all been destroyed 
		//and removed from the array m_DirectoriesToWatch
		if( m_DirectoriesToWatch.size() > 0 )
		{
			for(unsigned int i = 0; i < m_DirectoriesToWatch.size(); ++i)
			{
				assert( m_DirectoriesToWatch[i] == NULL );
			}
		}
#endif
	}
	return FALSE;
}

BOOL CDirectoryChangeWatcher::UnwatchDirectory(const std::wstring & strDirToStopWatching)
/***************************************************************
FUNCTION:	UnwatchDirectory(const CString & strDirToStopWatching -- if this directory is being watched, the watch is stopped

****************************************************************/
{
	BOOL bRetVal = FALSE;



	if( m_hCompPort != NULL )//The io completion port must be open
	{
		assert( !strDirToStopWatching.empty() );
		
		CSingleLock lock(&m_csDirWatchInfo, TRUE);
		int nIdx = -1;
		CDirWatchInfo * pDirInfo = GetDirWatchInfo(strDirToStopWatching, nIdx);
		if( pDirInfo != NULL
		&&	nIdx != -1 )
		{

			//stop watching this directory
			pDirInfo->UnwatchDirectory( m_hCompPort );

			//cleanup the object used to watch the directory
			m_DirectoriesToWatch[nIdx]=NULL;
			pDirInfo->DeleteSelf(this);
			bRetVal = TRUE;
		}
	}

	return bRetVal;
}

BOOL CDirectoryChangeWatcher::UnwatchDirectory(CDirectoryChangeHandler * pChangeHandler)
/************************************

  This function is called from the dtor of CDirectoryChangeHandler automatically,
  but may also be called by a programmer because it's public...
  
  A single CDirectoryChangeHandler may be used for any number of watched directories.

  Unwatch any directories that may be using this 
  CDirectoryChangeHandler * pChangeHandler to handle changes to a watched directory...
  
  The CDirWatchInfo::m_pChangeHandler member of objects in the m_DirectoriesToWatch
  array will == pChangeHandler if that handler is being used to handle changes to a directory....
************************************/
{
	assert( pChangeHandler );

	CSingleLock lock(&m_csDirWatchInfo, TRUE);


	
	int nUnwatched = 0;
	int nIdx = -1;
	CDirWatchInfo * pDirInfo;
	//
	//	go through and unwatch any directory that is 
	//	that is using this pChangeHandler as it's file change notification handler.
	//
	while( (pDirInfo = GetDirWatchInfo( pChangeHandler, nIdx )) != NULL )
	{
		pDirInfo->UnwatchDirectory( m_hCompPort );

		nUnwatched++;
		m_DirectoriesToWatch[nIdx] =NULL;
		pDirInfo->DeleteSelf(this);	
	}
	return (BOOL)(nUnwatched != 0);
}

BOOL CDirectoryChangeWatcher::UnwatchDirectoryBecauseOfError(CDirWatchInfo * pWatchInfo)
//
//	Called in the worker thread in the case that ReadDirectoryChangesW() fails
//	during normal operation. One way to force this to happen is to watch a folder
//	using a UNC path and changing that computer's IP address.
//	
{
	assert( pWatchInfo );
	BOOL bRetVal = FALSE;
	if( pWatchInfo )
	{
		CSingleLock lock(&m_csDirWatchInfo, TRUE);
	

		int nIdx = -1;
		if( GetDirWatchInfo(pWatchInfo, nIdx) == pWatchInfo )
		{
			// we are actually watching this....

			//
			//	Remove this CDirWatchInfo object from the list of watched directories.
			//
			m_DirectoriesToWatch[nIdx]=NULL;//mark the space as free for the next watch...

			//
			//	and delete it...
			//

			pWatchInfo->DeleteSelf(this);
		
		}

	}
	return bRetVal;
}

int	CDirectoryChangeWatcher::AddToWatchInfo(CDirectoryChangeWatcher::CDirWatchInfo * pWatchInfo)
//
//	
//	To add the CDirWatchInfo  * to an array.
//	The array is used to keep track of which directories 
//	are being watched.
//
//	Add the ptr to the first non-null slot in the array.
{
	CSingleLock lock( &m_csDirWatchInfo, TRUE);

	//first try to add it to the first empty slot in m_DirectoriesToWatch
	int max = (int)m_DirectoriesToWatch.size();
    int i;
	for(i = 0; i < max; ++i)
	{
		if( m_DirectoriesToWatch[i] == NULL )
		{
			m_DirectoriesToWatch[i] = pWatchInfo;
			break;
		}
	}
	if( i == max )
	{
                m_DirectoriesToWatch.push_back( pWatchInfo );
	}

	return (BOOL)(i != -1);
}

//
//	functions for retrieving the directory watch info based on different parameters
//
CDirectoryChangeWatcher::CDirWatchInfo * CDirectoryChangeWatcher::GetDirWatchInfo(const std::wstring & strDirName, int & ref_nIdx)const
{
	if( strDirName.empty() )// can't be watching a directory if it's you don't pass in the name of it...
		return FALSE;		  //
	
	CSingleLock lock(const_cast<CCriticalSection*>(&m_csDirWatchInfo), TRUE);

	int max = (int)m_DirectoriesToWatch.size();
	CDirWatchInfo * p = NULL;
	for(int i = 0; i < max; ++i )
	{
		if( (p = m_DirectoriesToWatch[i]) != NULL
		&&	strlower(p->m_strDirName)==strlower(strDirName) )
		{
			ref_nIdx = i;
			return p;
		}
	}
			
	return NULL;//NOT FOUND
}

CDirectoryChangeWatcher::CDirWatchInfo * CDirectoryChangeWatcher::GetDirWatchInfo(CDirectoryChangeWatcher::CDirWatchInfo * pWatchInfo, int & ref_nIdx)const
{
	assert( pWatchInfo != NULL );

	CSingleLock lock( const_cast<CCriticalSection*>(&m_csDirWatchInfo), TRUE);
	int i(0), max = (int)m_DirectoriesToWatch.size();
	CDirWatchInfo * p;
	for(; i < max; ++i)
	{
		if( (p = m_DirectoriesToWatch[i]) != NULL
		&&	 p == pWatchInfo )
		{
			ref_nIdx = i;
			return p;
		}
	}
	return NULL;//NOT FOUND
}

CDirectoryChangeWatcher::CDirWatchInfo * CDirectoryChangeWatcher::GetDirWatchInfo(CDirectoryChangeHandler * pChangeHandler, int & ref_nIdx)const
{
	assert( pChangeHandler != NULL );
	CSingleLock lock( const_cast<CCriticalSection*>(&m_csDirWatchInfo), TRUE);
	int i(0),max = (int)m_DirectoriesToWatch.size();
	CDirWatchInfo * p;
	for( ; i < max; ++i)
	{
		if( (p = m_DirectoriesToWatch[i]) != NULL
		&&	p->GetRealChangeHandler() == pChangeHandler )
		{
			ref_nIdx = i;
			return p;
		}
	}
	return NULL;//NOT FOUND
}

long CDirectoryChangeWatcher::ReleaseReferenceToWatcher(CDirectoryChangeHandler * pChangeHandler)
{
	assert( pChangeHandler );
	return pChangeHandler->ReleaseReferenceToWatcher(this);
}

CDirectoryChangeWatcher::CDirWatchInfo::CDirWatchInfo(HANDLE hDir, 
													  const std::wstring & strDirectoryName, 
													  CDirectoryChangeHandler * pChangeHandler,
													  DWORD dwChangeFilter, 
													  BOOL bWatchSubDir,
													  bool bAppHasGUI,
													  LPCTSTR szIncludeFilter,
													  LPCTSTR szExcludeFilter,
													  DWORD dwFilterFlags)
 :	m_pChangeHandler( NULL ), 
	m_hDir(hDir),
	m_dwChangeFilter( dwChangeFilter ),
	m_bWatchSubDir( bWatchSubDir ),
	m_strDirName( strDirectoryName ),
	m_dwBufLength(0),
	m_dwReadDirError(ERROR_SUCCESS),
	m_RunningState( RUNNING_STATE_NOT_SET )
{ 
	
	assert( hDir != INVALID_HANDLE_VALUE
		&& !strDirectoryName.empty() );
	
	//
	//	This object 'decorates' the pChangeHandler passed in
	//	so that notifications fire in the context a thread other than
	//	CDirectoryChangeWatcher::MonitorDirectoryChanges()
	//
	//	Supports the include and exclude filters
	//
	//
	m_pChangeHandler = new CDelayedDirectoryChangeHandler( pChangeHandler, bAppHasGUI, szIncludeFilter, szExcludeFilter, dwFilterFlags );
	if( m_pChangeHandler ) 
		m_pChangeHandler->SetPartialPathOffset( m_strDirName );//to support FILTERS_CHECK_PARTIAL_PATH..this won't change for the duration of the watch, so set it once... HERE!
	assert( m_pChangeHandler );

	assert( GetChangeHandler() );
	assert( GetRealChangeHandler() );
	if( GetRealChangeHandler() )
		GetRealChangeHandler()->AddRef();
	
	memset(&m_Overlapped, 0, sizeof(m_Overlapped));
	//memset(m_Buffer, 0, sizeof(m_Buffer));
}

CDirectoryChangeWatcher::CDirWatchInfo::~CDirWatchInfo()
{
	if( GetChangeHandler() )
	{//If this call to CDirectoryChangeHandler::Release() causes m_pChangeHandler to delete itself,
		//the dtor for CDirectoryChangeHandler will call CDirectoryChangeWatcher::UnwatchDirectory( CDirectoryChangeHandler * ),
		//which will make try to delete this object again.
		//if m_pChangeHandler is NULL, it won't try to delete this object again...
		CDirectoryChangeHandler * pTmp = SetRealDirectoryChangeHandler( NULL );
		if( pTmp )
			pTmp->Release();
		else{
			assert( FALSE );
		}
	}
	
	CloseDirectoryHandle();

	delete m_pChangeHandler;
	m_pChangeHandler = NULL;
	
}
void CDirectoryChangeWatcher::CDirWatchInfo::DeleteSelf(CDirectoryChangeWatcher * pWatcher)
//
//	There's a reason for this function!
//
//	the dtor is private to enforce that it is used.
//
//
//	pWatcher can be NULL only if CDirecotryChangeHandler::ReferencesWatcher() has NOT been called.
//	ie: in certain sections of WatchDirectory() it's ok to pass this w/ NULL, but no where else.
//
{
	//ASSERT( pWatcher != NULL );


	assert( GetRealChangeHandler() );
	if( pWatcher )
	{
	//
	//
	//	Before this object is deleted, the CDirectoryChangeHandler object
	//	needs to release it's reference count to the CDirectoryChangeWatcher object.
	//	I might forget to do this since I getting rid of CDirWatchInfo objects
	//	in more than one place...hence the reason for this function.
	//
		pWatcher->ReleaseReferenceToWatcher( GetRealChangeHandler() );
	}
	
	delete this;
}

CDelayedDirectoryChangeHandler* CDirectoryChangeWatcher::CDirWatchInfo::GetChangeHandler() const 
{ 
	return m_pChangeHandler; 
}

CDirectoryChangeHandler * CDirectoryChangeWatcher::CDirWatchInfo::GetRealChangeHandler() const
//
//	The 'real' change handler is the CDirectoryChangeHandler object 
//	passed to CDirectoryChangeWatcher::WatchDirectory() -- this is the object
//	that really handles the changes.
//
{	
	assert( m_pChangeHandler ); 
	return m_pChangeHandler->GetRealChangeHandler(); 
}

CDirectoryChangeHandler * CDirectoryChangeWatcher::CDirWatchInfo::SetRealDirectoryChangeHandler(CDirectoryChangeHandler * pChangeHandler)
//
//	Allows you to switch out, at run time, which object really handles the change notifications.
//
{
	CDirectoryChangeHandler * pOld = GetRealChangeHandler();
	m_pChangeHandler->GetRealChangeHandler() = pChangeHandler; 
	return pOld;
}

BOOL CDirectoryChangeWatcher::CDirWatchInfo::CloseDirectoryHandle()
//
//	Closes the directory handle that was opened in CDirectoryChangeWatcher::WatchDirecotry()
//
//
{
	BOOL b = TRUE;
	if( m_hDir != INVALID_HANDLE_VALUE )
	{
		b = CloseHandle(m_hDir);
		m_hDir = INVALID_HANDLE_VALUE;
	}
	return b;
}

DWORD CDirectoryChangeWatcher::CDirWatchInfo::StartMonitor(HANDLE hCompPort)
/*********************************************
  Sets the running state of the object to perform the initial call to ReadDirectoryChangesW()
  , wakes up the thread waiting on GetQueuedCompletionStatus()
  and waits for an event to be set before returning....

  The return value is either ERROR_SUCCESS if ReadDirectoryChangesW is successfull,
  or is the value of GetLastError() for when ReadDirectoryChangesW() failed.
**********************************************/
{
	assert( hCompPort );

	//Guard the properties of this object 
	LockProperties();
	

		
	m_RunningState = RUNNING_STATE_START_MONITORING;//set the state member to indicate that the object is to START monitoring the specified directory
	PostQueuedCompletionStatus(hCompPort, sizeof(this), (DWORD)this, &m_Overlapped);//make the thread waiting on GetQueuedCompletionStatus() wake up

	UnlockProperties();//unlock this object so that the thread can get at them...

	//wait for signal that the initial call 
	//to ReadDirectoryChanges has been made
	DWORD dwWait = 0;
	do{
		dwWait = WaitForSingleObject(m_StartStopEvent, 10 * 1000);
		if( dwWait != WAIT_OBJECT_0 )
		{
			//
			//	shouldn't ever see this one....but just in case you do, notify me of the problem wesj@hotmail.com.
			//
			TRACE3(_T("WARNING! Possible lockup detected. FILE: %s Line: %d\n"), _T( __FILE__ ), __LINE__);
		}
	} while( dwWait != WAIT_OBJECT_0 );

	assert( dwWait == WAIT_OBJECT_0 );
	m_StartStopEvent.ResetEvent();
	
	return m_dwReadDirError;//This value is set in the worker thread when it first calls ReadDirectoryChangesW().
}

BOOL CDirectoryChangeWatcher::CDirWatchInfo::UnwatchDirectory(HANDLE hCompPort)
/*******************************************

    Sets the running state of the object to stop monitoring a directory,
	Causes the worker thread to wake up and to stop monitoring the dierctory
	
********************************************/
{
	assert( hCompPort );
	//
	// Signal that the worker thread is to stop watching the directory
	//
	if( SignalShutdown(hCompPort) )
	{
		//and wait for the thread to signal that it has shutdown
		return WaitForShutdown();

	}
	return FALSE;
}

BOOL CDirectoryChangeWatcher::CDirWatchInfo::SignalShutdown( HANDLE hCompPort )
//added to fix a bug -- this will be called normally by UnwatchDirectory(HANDLE)
//						and abnormally by the worker thread in the case that ReadDirectoryChangesW fails -- see code.
//
//	Signals the worker thread(via the I/O completion port) that it is to stop watching the 
//	directory for this object, and then returns.
//
{
	BOOL bRetVal = FALSE;
	assert( hCompPort );
	assert( m_hDir != INVALID_HANDLE_VALUE );
	//Lock the properties so that they aren't modified by another thread
	 LockProperties(); //unlikey to fail...
		
	//set the state member to indicate that the object is to stop monitoring the 
	//directory that this CDirWatchInfo is responsible for...
	m_RunningState = CDirectoryChangeWatcher::CDirWatchInfo::RUNNING_STATE_STOP;
	//put this object in the I/O completion port... GetQueuedCompletionStatus() will return it inside the worker thread.
	bRetVal = PostQueuedCompletionStatus(hCompPort, sizeof(CDirWatchInfo*), (DWORD)this, &m_Overlapped);

	if( !bRetVal )
	{
		TRACE(_T("PostQueuedCompletionStatus() failed! GetLastError(): %d\n"), GetLastError());
	}
	 UnlockProperties();
	
	return bRetVal;
}

BOOL CDirectoryChangeWatcher::CDirWatchInfo::WaitForShutdown()
//
//	This is to be called some time after SignalShutdown().
//	
//
{
	
	//Wait for the Worker thread to indicate that the watch has been stopped
	DWORD dwWait;
	bool bWMQuitReceived = false;
	do{
		dwWait	= MsgWaitForMultipleObjects(1, &m_StartStopEvent.m_hEvent, FALSE, 5000, QS_ALLINPUT);//wait five seconds
		switch( dwWait )
		{
		case WAIT_OBJECT_0:
			//handle became signalled!
			break;
		case WAIT_OBJECT_0 + 1:
			{
				//This thread awoke due to sent/posted message
				//process the message Q
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
						This appears to be causing quite a lot of pain, to quote Mustafa.

						//it's the WM_QUIT message, put it back in the Q and
						// exit this function
						PostMessage(msg.hwnd, msg.message, msg.wParam, msg.lParam );
						bWMQuitReceived = true;

						****/
						break;
					}
				}
			}break;
		case WAIT_TIMEOUT:
			{
				TRACE4(_T("WARNING: Possible Deadlock detected! ThreadID: %d File: %s Line: %d\n"), GetCurrentThreadId(), _T(__FILE__), __LINE__);
			}break;
		}//end switch(dwWait)
	}while( dwWait != WAIT_OBJECT_0 && !bWMQuitReceived );
		
	
	
	assert( dwWait == WAIT_OBJECT_0 || bWMQuitReceived);

	m_StartStopEvent.ResetEvent();
	
	return (BOOL) (dwWait == WAIT_OBJECT_0 || bWMQuitReceived);
}


void CDirectoryChangeWatcher::MonitorDirectoryChanges(LPVOID lpvThis)
/********************************************
   The worker thread function which monitors directory changes....
********************************************/
{
    DWORD numBytes;

    CDirWatchInfo * pdi;
    LPOVERLAPPED lpOverlapped;
    
	CDirectoryChangeWatcher * pThis = reinterpret_cast<CDirectoryChangeWatcher*>(lpvThis);
	assert( pThis );

	pThis->On_ThreadInitialize();


    do
    {
        // Retrieve the directory info for this directory
        // through the io port's completion key
        if( !GetQueuedCompletionStatus( pThis->m_hCompPort,
                                   &numBytes,
                                   (PULONG_PTR) &pdi,//<-- completion Key
                                   &lpOverlapped,
                                   INFINITE) )
		{//The io completion request failed...
		 //probably because the handle to the directory that
		 //was used in a call to ReadDirectoryChangesW has been closed.

			//
			//	calling pdi->CloseDirectoryHandle() will cause GetQueuedCompletionStatus() to return false.
			//  
			//
		}
		
		if ( pdi )//pdi will be null if I call PostQueuedCompletionStatus(m_hCompPort, 0,0,NULL);
        {
			//
			//	The following check to AfxIsValidAddress() should go off in the case
			//	that I have deleted this CDirWatchInfo object, but it was still in 
			//	"in the Queue" of the i/o completion port from a previous overlapped operation.
			//
			/***********************************
			The CDirWatchInfo::m_RunningState is pretty much the only member
			of CDirWatchInfo that can be modified from the other thread.
			The functions StartMonitor() and UnwatchDirecotry() are the functions that 
			can modify that variable.

			So that I'm sure that I'm getting the right value, 
			I'm using a critical section to guard against another thread modyfying it when I want
			to read it...
			
			************************************/
			bool bObjectShouldBeOk = true;
			try{
			    pdi->LockProperties();//don't give the main thread a chance to change this object
			}
			catch(...){
				//any sort of exception here indicates I've
				//got a hosed object.
				TRACE1(_T("CDirectoryChangeWatcher::MonitorDirectoryChanges() -- pdi->LockProperties() raised an exception!\n"));
				bObjectShouldBeOk = false;
			}
			if( bObjectShouldBeOk )
			{
										    //while we're working with this object...

				CDirWatchInfo::eRunningState Run_State = pdi->m_RunningState ;
				
				 pdi->UnlockProperties();//let another thread back at the properties...
				/***********************************
				 Unlock it so that there isn't a DEADLOCK if 
				 somebody tries to call a function which will 
				 cause CDirWatchInfo::UnwatchDirectory() to be called
				 from within the context of this thread (eg: a function called because of
				 the handler for one of the CDirectoryChangeHandler::On_Filexxx() functions)
		
				************************************/
				
				assert( pdi->GetChangeHandler() );
				switch( Run_State )
				{
				case CDirWatchInfo::RUNNING_STATE_START_MONITORING:
					{
						//Issue the initial call to ReadDirectoryChangesW()
						
						if( !ReadDirectoryChangesW( pdi->m_hDir,
											pdi->m_Buffer,//<--FILE_NOTIFY_INFORMATION records are put into this buffer
											READ_DIR_CHANGE_BUFFER_SIZE,
											pdi->m_bWatchSubDir,
											pdi->m_dwChangeFilter,
											&pdi->m_dwBufLength,//this var not set when using asynchronous mechanisms...
											&pdi->m_Overlapped,
											NULL) )//no completion routine!
						{
							pdi->m_dwReadDirError = GetLastError();
							if( pdi->GetChangeHandler() )
								pdi->GetChangeHandler()->On_WatchStarted(pdi->m_dwReadDirError, pdi->m_strDirName);
						}
						else
						{//read directory changes was successful!
						 //allow it to run normally
							pdi->m_RunningState = CDirWatchInfo::RUNNING_STATE_NORMAL;
							pdi->m_dwReadDirError = ERROR_SUCCESS;
							if( pdi->GetChangeHandler() )
								pdi->GetChangeHandler()->On_WatchStarted(ERROR_SUCCESS, pdi->m_strDirName );
						}
						pdi->m_StartStopEvent.SetEvent();//signall that the ReadDirectoryChangesW has been called
														 //check CDirWatchInfo::m_dwReadDirError to see whether or not ReadDirectoryChangesW succeeded...

						//
						//	note that pdi->m_dwReadDirError is the value returned by WatchDirectory()
						//
						
		
					}break;
				case CDirWatchInfo::RUNNING_STATE_STOP:
					{
						//We want to shut down the monitoring of the directory
						//that pdi is managing...
						
						if( pdi->m_hDir != INVALID_HANDLE_VALUE )
						{
						 //Since I've previously called ReadDirectoryChangesW() asynchronously, I am waiting
						 //for it to return via GetQueuedCompletionStatus().  When I close the
						 //handle that ReadDirectoryChangesW() is waiting on, it will
						 //cause GetQueuedCompletionStatus() to return again with this pdi object....
						 // Close the handle, and then wait for the call to GetQueuedCompletionStatus()
						 //to return again by breaking out of the switch, and letting GetQueuedCompletionStatus()
						 //get called again
							pdi->CloseDirectoryHandle();
							pdi->m_RunningState = CDirWatchInfo::RUNNING_STATE_STOP_STEP2;//back up step...GetQueuedCompletionStatus() will still need to return from the last time that ReadDirectoryChangesW() was called.....

						 //
						 //	The watch has been stopped, tell the client about it
						 //						if( pdi->GetChangeHandler() )
							pdi->GetChangeHandler()->On_WatchStopped( pdi->m_strDirName );
						}
						else
						{
							//either we weren't watching this direcotry in the first place,
							//or we've already stopped monitoring it....
							pdi->m_StartStopEvent.SetEvent();//set the event that ReadDirectoryChangesW has returned and no further calls to it will be made...
						}
						
					
					}break;
				case CDirWatchInfo::RUNNING_STATE_STOP_STEP2:
					{

						//GetQueuedCompletionStatus() has returned from the last
						//time that ReadDirectoryChangesW was called...
						//Using CloseHandle() on the directory handle used by
						//ReadDirectoryChangesW will cause it to return via GetQueuedCompletionStatus()....
						if( pdi->m_hDir == INVALID_HANDLE_VALUE )
							pdi->m_StartStopEvent.SetEvent();//signal that no further calls to ReadDirectoryChangesW will be made
															 //and this pdi can be deleted 
						else
						{//for some reason, the handle is still open..
												
							pdi->CloseDirectoryHandle();

							//wait for GetQueuedCompletionStatus() to return this pdi object again


						}
		
					}break;
															
				case CDirWatchInfo::RUNNING_STATE_NORMAL:
					{
						
						if( pdi->GetChangeHandler() )
							pdi->GetChangeHandler()->SetChangedDirectoryName( pdi->m_strDirName );
		
						DWORD dwReadBuffer_Offset = 0UL;

						//process the FILE_NOTIFY_INFORMATION records:
						CFileNotifyInformation notify_info( (LPBYTE)pdi->m_Buffer, READ_DIR_CHANGE_BUFFER_SIZE);

						pThis->ProcessChangeNotifications(notify_info, pdi, dwReadBuffer_Offset);
		

						//	Changes have been processed,
						//	Reissue the watch command
						//
						if( !ReadDirectoryChangesW( pdi->m_hDir,
											  pdi->m_Buffer + dwReadBuffer_Offset,//<--FILE_NOTIFY_INFORMATION records are put into this buffer 
								              READ_DIR_CHANGE_BUFFER_SIZE - dwReadBuffer_Offset,
											  pdi->m_bWatchSubDir,
										      pdi->m_dwChangeFilter,
											  &pdi->m_dwBufLength,//this var not set when using asynchronous mechanisms...
											&pdi->m_Overlapped,
											NULL) )//no completion routine!
						{
							//
							//	NOTE:  
							//		In this case the thread will not wake up for 
							//		this pdi object because it is no longer associated w/
							//		the I/O completion port...there will be no more outstanding calls to ReadDirectoryChangesW
							//		so I have to skip the normal shutdown routines(normal being what happens when CDirectoryChangeWatcher::UnwatchDirectory() is called.
							//		and close this up, & cause it to be freed.
							//
							TRACE(_T("WARNING: ReadDirectoryChangesW has failed during normal operations...failed on directory: %s\n"), pdi->m_strDirName);

							assert( pThis );
							//
							//	To help insure that this has been unwatched by the time
							//	the main thread processes the On_ReadDirectoryChangesError() notification
							//	bump the thread priority up temporarily.  The reason this works is because the notification
							//	is really posted to another thread's message queue,...by setting this thread's priority
							//	to highest, this thread will get to shutdown the watch by the time the other thread has a chance
							//	to handle it. *note* not technically guaranteed 100% to be the case, but in practice it'll work.
							int nOldThreadPriority = GetThreadPriority( GetCurrentThread() );
							SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);							

							//
							//	Notify the client object....(a CDirectoryChangeHandler derived class)
							//
							try{
								pdi->m_dwReadDirError = GetLastError();
								pdi->GetChangeHandler()->On_ReadDirectoryChangesError( pdi->m_dwReadDirError, pdi->m_strDirName );


								//Do the shutdown
								pThis->UnwatchDirectoryBecauseOfError( pdi );
								//pdi = NULL; <-- DO NOT set this to NULL, it will cause this worker thread to exit.
								//pdi is INVALID at this point!!
							}
							catch(...)
							{
								//just in case of exception, this thread will be set back to 
								//normal priority.
							}
							//
							//	Set the thread priority back to normal.
							//
							SetThreadPriority(GetCurrentThread(), nOldThreadPriority);
													
						}
						else
						{//success, continue as normal
							pdi->m_dwReadDirError = ERROR_SUCCESS;
						}
					}break;
				default:
					TRACE1(_T("MonitorDirectoryChanges() -- how did I get here?\n"));
					break;//how did I get here?
				}//end switch( pdi->m_RunningState )
		
		
		
			}//end if( bObjectShouldBeOk )
        }//end if( pdi )
    } while( pdi );

	pThis->On_ThreadExit();
	return; //thread is ending
}

void CDirectoryChangeWatcher::ProcessChangeNotifications(IN CFileNotifyInformation & notify_info, 
														 IN CDirectoryChangeWatcher::CDirWatchInfo * pdi,
														 OUT DWORD & ref_dwReadBuffer_Offset//used in case ...see case for FILE_ACTION_RENAMED_OLD_NAME
														 )
/////////////////////////////////////////////////////////////
//
//	Processes the change notifications and dispatches the handling of the 
//	notifications to the CDirectoryChangeHandler object passed to WatchDirectory()
//
/////////////////////////////////////////////////////////////
{
	//
	//	Sanity check...
	//	this function should only be called by the worker thread.
	//


	//	Validate parameters...
	//	
	assert( pdi );



	DWORD dwLastAction = 0;
	ref_dwReadBuffer_Offset = 0UL;
	

	CDirectoryChangeHandler * pChangeHandler = pdi->GetChangeHandler();
	//CDelayedDirectoryChangeHandler * pChangeHandler = pdi->GetChangeHandler();
	assert( pChangeHandler );
	//ASSERT( AfxIsValidAddress(pChangeHandler, sizeof(CDelayedDirectoryChangeHandler)) );
	if( !pChangeHandler )
	{
		TRACE1(_T("CDirectoryChangeWatcher::ProcessChangeNotifications() Unable to continue, pdi->GetChangeHandler() returned NULL!\n"));
		TRACE3(_T("File: %s  Line: %d\n"), _T( __FILE__ ), __LINE__ );
		return;
	}


	//
	//	go through and process the notifications contained in the
	//	CFileChangeNotification object( CFileChangeNotification is a wrapper for the FILE_NOTIFY_INFORMATION structure
	//									returned by ReadDirectoryChangesW)
	//
    do
	{
		//The FileName member of the FILE_NOTIFY_INFORMATION
		//structure contains the NAME of the file RELATIVE to the 
		//directory that is being watched...
		//ie, if watching C:\Temp and the file C:\Temp\MyFile.txt is changed,
		//the file name will be "MyFile.txt"
		//If watching C:\Temp, AND you're also watching subdirectories
		//and the file C:\Temp\OtherFolder\MyOtherFile.txt is modified,
		//the file name will be "OtherFolder\MyOtherFile.txt

		//The CDirectoryChangeHandler::On_Filexxx() functions will receive the name of the file
		//which includes the full path to the directory being watched
		
		
		//	
		//	See what the change was
		//
		
		switch( notify_info.GetAction() )
		{
		case FILE_ACTION_ADDED:		// a file was added!
	
			pChangeHandler->On_FileAdded( notify_info.GetFileNameWithPath( pdi->m_strDirName ) ); break;

		case FILE_ACTION_REMOVED:	//a file was removed
		
			pChangeHandler->On_FileRemoved( notify_info.GetFileNameWithPath( pdi->m_strDirName ) ); break;

		case FILE_ACTION_MODIFIED:
			//a file was changed
			//pdi->m_pChangeHandler->On_FileModified( strLastFileName ); break;
			pChangeHandler->On_FileModified( notify_info.GetFileNameWithPath( pdi->m_strDirName ) ); break;

		case FILE_ACTION_RENAMED_OLD_NAME:
			{//a file name has changed, and this is the OLD name
			 //This record is followed by another one w/
			 //the action set to FILE_ACTION_RENAMED_NEW_NAME (contains the new name of the file

				std::wstring strOldFileName = notify_info.GetFileNameWithPath( pdi->m_strDirName );

				
				if( notify_info.GetNextNotifyInformation() )
				{//there is another PFILE_NOTIFY_INFORMATION record following the one we're working on now...
				 //it will be the record for the FILE_ACTION_RENAMED_NEW_NAME record
			

					//assert( notify_info.GetAction() == FILE_ACTION_RENAMED_NEW_NAME );//making sure that the next record after the OLD_NAME record is the NEW_NAME record

					//get the new file name
					std::wstring strNewFileName = notify_info.GetFileNameWithPath( pdi->m_strDirName );

					pChangeHandler->On_FileNameChanged( strOldFileName, strNewFileName);
				}
				else
				{
					//this OLD_NAME was the last record returned by ReadDirectoryChangesW
					//I will have to call ReadDirectoryChangesW again so that I will get 
					//the record for FILE_ACTION_RENAMED_NEW_NAME

					//Adjust an offset so that when I call ReadDirectoryChangesW again,
					//the FILE_NOTIFY_INFORMATION will be placed after 
					//the record that we are currently working on.

					/***************
					Let's say that 200 files all had their names changed at about the same time
					There will be 400 FILE_NOTIFY_INFORMATION records (one for OLD_NAME and one for NEW_NAME for EACH file which had it's name changed)
					that ReadDirectoryChangesW will have to report to
					me.   There might not be enough room in the buffer
					and the last record that we DID get was an OLD_NAME record,
					I will need to call ReadDirectoryChangesW again so that I will get the NEW_NAME 
					record.    This way I'll always have to strOldFileName and strNewFileName to pass
					to CDirectoryChangeHandler::On_FileRenamed().

				   After ReadDirecotryChangesW has filled out our buffer with
				   FILE_NOTIFY_INFORMATION records,
				   our read buffer would look something like this:
																						 End Of Buffer
																							  |
																							 \-/	
					|_________________________________________________________________________
					|																		  |
					|file1 OLD name record|file1 NEW name record|...|fileX+1 OLD_name record| |(the record we want would be here, but we've ran out of room, so we adjust an offset and call ReadDirecotryChangesW again to get it) 
					|_________________________________________________________________________|

					Since the record I need is still waiting to be returned to me,
					and I need the current 'OLD_NAME' record,
					I'm copying the current FILE_NOTIFY_INFORMATION record 
					to the beginning of the buffer used by ReadDirectoryChangesW()
					and I adjust the offset into the read buffer so the the NEW_NAME record
					will be placed into the buffer after the OLD_NAME record now at the beginning of the buffer.

					Before we call ReadDirecotryChangesW again,
					modify the buffer to contain the current OLD_NAME record...

					|_______________________________________________________
					|														|
					|fileX old name record(saved)|<this is now garbage>.....|
					|_______________________________________________________|
											 	 /-\
												  |
											 Offset for Read
					Re-issue the watch command to get the rest of the records...

					ReadDirectoryChangesW(..., pBuffer + (an Offset),

					After GetQueuedCompletionStatus() returns, 
					our buffer will look like this:

					|__________________________________________________________________________________________________________
					|																										   |
					|fileX old name record(saved)|fileX new name record(the record we've been waiting for)| <other records>... |
					|__________________________________________________________________________________________________________|

					Then I'll be able to know that a file name was changed
					and I will have the OLD and the NEW name of the file to pass to CDirectoryChangeHandler::On_FileNameChanged

					****************/
					//NOTE that this case has never happened to me in my testing
					//so I can only hope that the code works correctly.
					//It would be a good idea to set a breakpoint on this line of code:
					notify_info.CopyCurrentRecordToBeginningOfBuffer( ref_dwReadBuffer_Offset ) ;
					

				}
				break;
			}
		case FILE_ACTION_RENAMED_NEW_NAME:
			{
				//This should have been handled in FILE_ACTION_RENAMED_OLD_NAME
				assert( dwLastAction == FILE_ACTION_RENAMED_OLD_NAME );
				assert( FALSE );//this shouldn't get here
			}
		
		default:
			TRACE(_T("CDirectoryChangeWatcher::ProcessChangeNotifications() -- unknown FILE_ACTION_ value! : %d\n"), notify_info.GetAction() );
			break;//unknown action
		}

		dwLastAction = notify_info.GetAction();
		
    
	} while( notify_info.GetNextNotifyInformation() );
}