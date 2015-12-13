#ifndef ISESSIONMGR_H
#define ISESSIONMGR_H

#include <string>

#include "User.h"

class ISessionMgr
{
public:
	virtual std::string GenerateSessionIDWithUser(const std::string &pUsername, const std::string &pIdentData, bool update_user=false)=0;

	virtual SUser *getUser(const std::string &pSID, const std::string &pIdentData, bool update=true)=0;
	virtual void releaseUser(SUser *user)=0;
	virtual void lockUser(SUser *user)=0;

	virtual bool RemoveSession(const std::string &pSID)=0;
};

#endif //ISESSIONMGR_H