#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include <mutex>

class ClientMain;
struct SProcess;

class ServerPingThread : public IThread
{
public:
	ServerPingThread(ClientMain *client_main, const std::string& clientname,
		size_t status_id, bool with_eta, std::string server_token, bool with_xfer_stats=false);
	void operator()(void);
	void setStop(bool b);

	bool isTimeout(void);

	//Share of the last ping interval the client's file server spent waiting for disk reads
	//and for network sends (summed over its connections). False if the client does not
	//report it (older client) or the last report is not recent.
	bool getClientWaitShares(double& read_share, double& send_share);

private:
	void setPaused(SProcess* proc, bool b);
	void addXferSample(const std::string& params_str);

	bool with_xfer_stats;
	std::mutex xfer_mutex;
	size_t n_xfer_samples;
	int64 xfer_time[2];
	int64 xfer_read[2];
	int64 xfer_send[2];
	ClientMain *client_main;
	volatile bool stop;
	volatile bool is_timeout;
	bool with_eta;
	std::string clientname;
	size_t status_id;
	std::string server_token;
};