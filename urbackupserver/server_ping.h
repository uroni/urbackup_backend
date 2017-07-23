#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"

class ClientMain;
struct SProcess;

class ServerPingThread : public IThread
{
public:
	ServerPingThread(ClientMain *client_main, const std::string& clientname,
		size_t status_id, bool with_eta, std::string server_token);
	void operator()(void);
	void setStop(bool b);

	bool isTimeout(void);

private:
	void setPaused(SProcess* proc, bool b);
	ClientMain *client_main;
	volatile bool stop;
	volatile bool is_timeout;
	bool with_eta;
	std::string clientname;
	size_t status_id;
	std::string server_token;
};