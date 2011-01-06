#ifndef __NTSERVICE_INCLUDED__
#define __NTSERVICE_INCLUDED__

#include "stdh.h"

/* 
  
  This class implements a simple NT service.
  The user have to supply the callback functions (main at least) and the
  the class does all the necessary job to put a service to run. Its left
  to the user  only what really matters: the implementation of service_main
  and and of control handlers

 */

namespace nt
{

typedef void (*NTSERVICE_CALLBACK_FUNCTION)(void);

typedef DWORD NTSERVICE_CONTROL;
typedef DWORD NTSERVICE_ACCEPT;

class nt_service
{

public:
	
	static nt_service&  instance    ( char_ * service_name );
		
	void register_service_main      ( LPSERVICE_MAIN_FUNCTION );
	void register_control_handler   ( NTSERVICE_CONTROL, NTSERVICE_CALLBACK_FUNCTION );
	void register_init_function     ( NTSERVICE_CALLBACK_FUNCTION );
	void accept_control				( NTSERVICE_ACCEPT );	
	
	void		start( void );
	static void stop ( DWORD exit_code = 0 );	

private:

	// this is kept private to guarantee that only one instance of this class exists
	// its implemented as a meyer's sigletom 
	// see alexandrescu's modern C++ book for reference

	nt_service(){};
	~nt_service(){};
	nt_service( const nt_service & );
	const nt_service & operator=( const nt_service & );
};
}
#endif
