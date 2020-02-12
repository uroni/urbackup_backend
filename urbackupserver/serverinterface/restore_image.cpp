#include "action_header.h"
#include "../dao/ServerBackupDao.h"
#include "../server_channel.h"
#include "../server_settings.h"

namespace
{
	class RemoveRestoreToken : public IObject
	{
		std::string token;
	public:
		RemoveRestoreToken(std::string token)
			: token(token)
		{}

		~RemoveRestoreToken()
		{
			ServerChannelThread::remove_restore_token(token);
		}
	};
}

ACTION_IMPL(restore_image)
{
	Helper helper(tid, &POST, &PARAMS);

	SUser* session = helper.getSession();
	if (session != NULL && session->id == SESSION_ID_INVALID) return;

	JSON::Object ret;
	if (session == NULL)
	{
		ret.set("error", 1);
		helper.Write(ret.stringify(false));
		return;
	}

	bool all_browse_backups;
	std::vector<int> browse_backups_rights = helper.clientRights(RIGHT_BROWSE_BACKUPS, all_browse_backups);

	ServerBackupDao dao(helper.getDatabase());

	int backupid = watoi(POST["backupid"]);
	ServerBackupDao::CondInt clientid = dao.getClientidByImageid(backupid);

	if (!clientid.exists)
	{
		return;
	}

	if (!all_browse_backups
		&& std::find(browse_backups_rights.begin(), browse_backups_rights.end(), clientid.value) == browse_backups_rights.end())
	{
		return;
	}

	db_results restore_authkey = helper.getDatabase()->Read("SELECT value FROM settings_db.settings WHERE key='restore_authkey' AND clientid=0");

	if (restore_authkey.size() != 1)
	{
		return;
	}

	std::string token = ServerSettings::generateRandomAuthKey();

	ServerChannelThread::add_restore_token(token, backupid);

	IObject* curr = helper.getSession()->getCustomPtr("rm_restore_token");
	if (curr != NULL)
	{
		curr->Remove();
	}

	helper.getSession()->mCustom["rm_restore_token"] = new RemoveRestoreToken(token);

	ret.set("ok", true);
	ret.set("token", token);
	ret.set("authkey", restore_authkey[0]["value"]);
	helper.Write(ret.stringify(false));
}