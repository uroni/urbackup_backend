#include "nt_service.h"
#include "bitmask.h"
#include <map>

using namespace nt;
using std_::bitmask;
using std::map;

// This functions and variables were part of nt_service class.
// I choose to remove them to keep it's interface clean and simple.

namespace service_impl
{

char_*						service_name        = 0;
SERVICE_STATUS				service_status;
SERVICE_STATUS_HANDLE		hstatus		        = 0;
LPSERVICE_MAIN_FUNCTION		user_service_main   = 0;
NTSERVICE_CALLBACK_FUNCTION	service_init_fcn    = 0;

map< NTSERVICE_CONTROL , NTSERVICE_CALLBACK_FUNCTION > callback_map;
bitmask< NTSERVICE_ACCEPT > accepted_controls;

// this function is used when the to converto from a SERVICE_CONTROL_* constant to a
// SERVICE_ACCEPT_* constant. This functions is used by the class to the to accept a
// control when the user register a function to handle it.

NTSERVICE_ACCEPT accept_const_from_control_const( NTSERVICE_CONTROL control )
{
	switch( control )
	{
		case SERVICE_CONTROL_STOP:
			return SERVICE_ACCEPT_STOP;

		case SERVICE_CONTROL_PAUSE:
		case SERVICE_CONTROL_CONTINUE:
			return SERVICE_ACCEPT_PAUSE_CONTINUE;

		case SERVICE_CONTROL_SHUTDOWN:
			return SERVICE_ACCEPT_SHUTDOWN;

		case SERVICE_CONTROL_PARAMCHANGE:
			return SERVICE_ACCEPT_PARAMCHANGE;

		case SERVICE_CONTROL_NETBINDADD:
		case SERVICE_CONTROL_NETBINDREMOVE:
		case SERVICE_CONTROL_NETBINDENABLE:
		case SERVICE_CONTROL_NETBINDDISABLE:
			return SERVICE_ACCEPT_NETBINDCHANGE;

		case SERVICE_CONTROL_HARDWAREPROFILECHANGE:
			return SERVICE_ACCEPT_HARDWAREPROFILECHANGE;

		case SERVICE_CONTROL_POWEREVENT:
			return SERVICE_ACCEPT_POWEREVENT;

		case SERVICE_CONTROL_SESSIONCHANGE:
			return SERVICE_ACCEPT_SESSIONCHANGE;	

		case SERVICE_CONTROL_INTERROGATE:
		case SERVICE_CONTROL_DEVICEEVENT:
		default:
			return NTSERVICE_ACCEPT(0);

	}
}
//-----------------------------------------------------------------------------
VOID WINAPI service_control_handler(DWORD control)
{
	if ( callback_map.find(control) != callback_map.end() )
		callback_map[control]();

	switch(control) 
    { 
        case SERVICE_CONTROL_STOP: 
            service_status.dwCurrentState  = SERVICE_STOPPED; 
            break;
        case SERVICE_CONTROL_SHUTDOWN: 
            service_status.dwCurrentState  = SERVICE_STOPPED; 
            break;
		case SERVICE_CONTROL_PAUSE:
			service_status.dwCurrentState  = SERVICE_PAUSED; 
        default:
			service_status.dwCurrentState  = SERVICE_RUNNING; 
            break;
	}
	service_status.dwWin32ExitCode = 0; 
    SetServiceStatus (hstatus, &service_status);
}
//-----------------------------------------------------------------------------
VOID WINAPI nt_service_main( DWORD argc, char_* argv[] )
{
	service_status.dwServiceType        = SERVICE_WIN32; 
    service_status.dwCurrentState       = SERVICE_START_PENDING; 
	service_status.dwControlsAccepted   = accepted_controls.to_dword();
     
    hstatus = RegisterServiceCtrlHandler( service_name, service_control_handler );

    if ( hstatus == 0 ) 
    { 
		return; 
    }  
    
	if ( service_init_fcn )
	{
		try
		{
			service_init_fcn();
		}
		catch(...)
		{
			nt_service::stop(-1);
			return;
		}
	}
	    
    // We report the running status to SCM. 
    service_status.dwCurrentState = SERVICE_RUNNING; 
    SetServiceStatus( hstatus, &service_status);
     
    // The worker loop of a service
    while ( service_status.dwCurrentState == SERVICE_RUNNING )
	{
		try
		{
			user_service_main( argc, argv );
		}
		catch(...)
		{
			nt_service::stop( -1 );
		}
	}
    return; 
}

} // end of namespace service_impl
