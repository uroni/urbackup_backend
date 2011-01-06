#include <string>
#include "../Interface/Object.h"
#include "../Interface/Pipe.h"
#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"

#include "IPipedProcess.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

class CPipedProcess : public IThread, public IPipedProcess
{
public:
	CPipedProcess(std::string cmd);
	~CPipedProcess();

	bool isOpen(void);
	bool isOpenInt(void);

	void operator()(void);

	bool Write(const std::string &str);
	std::string Read(void);

	IPipe *getOutputPipe();

private:

#ifdef _WIN32
	HANDLE g_hChildStd_IN_Rd;
	HANDLE g_hChildStd_IN_Wr;
	HANDLE g_hChildStd_OUT_Rd;
	HANDLE g_hChildStd_OUT_Wr;

	PROCESS_INFORMATION piProcInfo; 
	IMutex *mutex;
#else
	int inputp;
	FILE *outputp;
	std::string fifo;
	pid_t pid;
	int return_code;
	IMutex *mutex;
#endif

	IPipe *output;
	bool stop_thread;
	bool thread_stopped;

	bool is_open;

};
