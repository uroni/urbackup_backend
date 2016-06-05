#include "action_header.h"
#include "../../Interface/ThreadPool.h"

ACTION_IMPL(restore_prepare_wait)
{
	Helper helper(tid, &POST, &PARAMS);
	JSON::Object ret;
	SUser *session = helper.getSession();

	if (session != NULL)
	{
		THREADPOOL_TICKET ticket = static_cast<THREADPOOL_TICKET>(watoi64(session->mStr[POST["wait_key"]]));

		if (Server->getThreadPool()->waitFor(ticket, 10000))
		{
			session->mStr.erase(session->mStr.find("wait_key"));
			ret.set("completed", true);
		}
		else
		{
			ret.set("completed", false);
		}

		ret.set("wait_key", POST["wait_key"]);
	}
	else
	{
		ret.set("error", 1);
	}

	helper.Write(ret.stringify(false));
}