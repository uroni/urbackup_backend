#include "action_header.h"

ACTION_IMPL(logout)
{
    Helper helper(tid, &POST, &PARAMS);

    SUser* session = helper.getSession();

    if(session==NULL)
    {
        JSON::Object ret;
        ret.set("error", JSON::Value(1));
        helper.Write(ret.stringify(false));
        return;
    }

    std::string ses = session->session;

    helper.releaseAll();
    Server->getSessionMgr()->RemoveSession(ses);

    JSON::Object ret;
    ret.set("success", JSON::Value(true));
    helper.Write(ret.stringify(false));
}
