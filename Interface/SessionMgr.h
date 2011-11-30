#ifndef ISESSIONMGR_H
#define ISESSIONMGR_H

#include <string>

#include "User.h"

class ISessionMgr
{
public:
	virtual std::wstring GenerateSessionIDWithUser(const std::wstring &pUsername, const std::wstring &pIdentData, bool update_user=false)=0;

	virtual SUser *getUser(const std::wstring &pSID, const std::wstring &pIdentData, bool update=true)=0;
	virtual void releaseUser(SUser *user)=0;

	virtual bool RemoveSession(const std::wstring &pSID)=0;
};

#endif //ISESSIONMGR_H