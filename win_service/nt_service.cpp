#ifdef _WIN32
#include "nt_service.h"
#include "nt_service_impl.h"

using namespace service_impl;
// meyers singletom implementation
nt_service& nt_service::instance(char_ * name )
{
	service_name = name;
	static  nt_service obj; 
	return  obj;
}

void nt_service::register_service_main( LPSERVICE_MAIN_FUNCTION service_main )
{
	user_service_main = service_main;
}

void nt_service::register_control_handler( NTSERVICE_CONTROL			control,
										   NTSERVICE_CALLBACK_FUNCTION	ctrl_handler )
{
	if (ctrl_handler)
	{
		callback_map[control] = ctrl_handler;
		accepted_controls << accept_const_from_control_const( control );
	}
}

void nt_service::register_init_function( NTSERVICE_CALLBACK_FUNCTION  init_fcn)
{
	service_init_fcn = init_fcn;
}
void nt_service::accept_control( NTSERVICE_ACCEPT control )
{
	accepted_controls << control;	
}
void nt_service::start()
{
	SERVICE_TABLE_ENTRY service_table[2];
    
	service_table[0].lpServiceName = service_name;
	service_table[0].lpServiceProc = nt_service_main;
	service_table[1].lpServiceName = 0;
    service_table[1].lpServiceProc = 0;

	StartServiceCtrlDispatcher(service_table);
}

void nt_service::stop(DWORD exit_code)
{
	if ( hstatus )
	{
		service_status.dwCurrentState  = SERVICE_STOPPED;
		service_status.dwWin32ExitCode = exit_code; 
		service_status.dwWaitHint = 0;
		SetServiceStatus ( hstatus, &service_status );
	}
}
#endif
