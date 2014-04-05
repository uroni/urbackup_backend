#include <vector>
#include <string>
#include "../Interface/Types.h"
#include "../Interface/CustomClient.h"
#include "../Interface/Mutex.h"

class IPipe;
class IThread;
class CHTTPProxy;

struct SShareProxy
{
	CHTTPProxy* proxy;
	THREADPOOL_TICKET proxy_ticket;
	IPipe *notify_pipe;
	IPipe *timeout_pipe;
};


class CHTTPClient : public ICustomClient
{
public:
	virtual void Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpoint);

	virtual void ReceivePackets(void);
	virtual bool Run(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

private:

	inline void processCommand(char ch);
	inline void processHeader(char ch);
	inline void processContent(char ch);
	inline bool processRequest(void);
	inline void reset(void);

	inline void WaitForRemove(void);

	inline std::vector<std::string> parseHTTPPath(std::string pPath);
	inline void parseAction(std::string pQuery, std::string &pAction, std::string &pContext);
	inline void ParseMultipartData(const std::string &data, const std::string &boundary);

	str_nmap http_params;
	std::string http_method;
	std::string http_query;
	std::string http_content;
	int http_version;
	int http_g_state;
	int http_state;
	unsigned int http_keepalive_start;
	unsigned int http_keepalive_count;
	size_t http_remaining_content;
	std::string tmp;
	std::string http_header_key;
	bool fileupload;
	std::string endpoint;

	int request_num;

	IObject *request_handler;
	THREADPOOL_TICKET request_ticket;
	POSTFILE_KEY pfilekey;
	

	IPipe *pipe;
	THREAD_ID tid;
	bool do_quit;

	static IMutex *share_mutex;
	static std::map<std::string, SShareProxy> shared_connections;
};
