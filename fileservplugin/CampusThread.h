#include "settings.h"

#ifdef CAMPUS

#include "../Interface/Thread.h"

class CCampusThread : public IThread
{
public:
	CCampusThread(std::string sname);
	void operator()(void);

private:
	std::string servername;
};

#endif //CAMPUS
