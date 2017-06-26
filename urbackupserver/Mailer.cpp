#include "Mailer.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "database.h"
#include "../urlplugin/IUrlFactory.h"
#include "ClientMain.h"
#include <math.h>

IMutex* Mailer::mutex = NULL;
ICondition* Mailer::cond = NULL;
bool Mailer::queue_limit = false;
bool Mailer::queued_mail = false;
extern IUrlFactory *url_fak;

bool Mailer::sendMail(const std::string & send_to, const std::string & subject, const std::string & message)
{
	IScopedLock lock(mutex);
	if (queue_limit)
		return false;

	lock.relock(NULL);

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	DBScopedSynchronous db_synchronous(db);
	IQuery* q = db->Prepare("INSERT INTO mail_queue (send_to, subject, message) VALUES (?,?,?)", false);
	q->Bind(send_to);
	q->Bind(subject);
	q->Bind(message);
	bool b = q->Write();

	if (b)
	{
		lock.relock(mutex);
		cond->notify_all();
		queued_mail = true;
	}

	db->destroyQuery(q);

	return b;
}

void Mailer::init()
{
	mutex = Server->createMutex();
	cond = Server->createCondition();
	Server->createThread(new Mailer, "mail queue");
}

void Mailer::operator()()
{
	if (url_fak == NULL)
		return;

	bool has_mail_server;
	{
		MailServer mail_server = ClientMain::getMailServerSettings();
		has_mail_server = !mail_server.servername.empty();
	}

	if (!has_mail_server)
	{
		queue_limit = true;
	}

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	IQuery* q_get_mail = db->Prepare("SELECT id, send_to, subject, message, next_try, retry_count FROM mail_queue WHERE next_try IS NULL or next_try>=?");
	IQuery* q_set_retry = db->Prepare("UPDATE mail_queue SET next_try=?, retry_count=? WHERE id=?");
	IQuery* q_remove_mail = db->Prepare("DELETE FROM mail_queue WHERE id=?");

	db->Write("UPDATE mail_queue SET next_try=NULL");

	while (true)
	{
		{
			IScopedLock lock(mutex);
			if (!queued_mail)
			{
				cond->wait(&lock, 5 * 60 * 1000);
			}
			queued_mail = false;
		}

		q_get_mail->Bind(Server->getTimeMS());
		db_results res = q_get_mail->Read();
		q_get_mail->Reset();

		if (res.size() > 1000)
		{
			IScopedLock lock(mutex);
			queue_limit = true;
		}
		else if (queue_limit
			&& has_mail_server)
		{
			IScopedLock lock(mutex);
			queue_limit = false;
		}

		MailServer mail_server;

		if (!res.empty() || !has_mail_server)
		{
			mail_server = ClientMain::getMailServerSettings();
		}

		if (mail_server.servername.empty())
		{
			has_mail_server = false;
			continue;
		}
		else
		{
			has_mail_server = true;
		}

		for (size_t i = 0; i < res.size(); ++i)
		{
			std::vector<std::string> addrs;
			Tokenize(res[i]["send_to"], addrs, ";,");

			std::string errmsg;
			if (!url_fak->sendMail(mail_server, addrs, res[i]["subject"], res[i]["message"], &errmsg))
			{
				int n = watoi(res[i]["retry_count"]);
				unsigned int waittime = (std::min)(static_cast<unsigned int>(1000.*pow(2., static_cast<double>(n))), (unsigned int)30 * 60 * 1000); //30min
				if (n>20)
				{
					waittime = (unsigned int)30 * 60 * 1000;
				}

				Server->Log("Error sending mail to \"" + res[i]["send_to"] + "\". " + errmsg + ". Retrying in " + PrettyPrintTime(waittime), LL_WARNING);

				q_set_retry->Bind(Server->getTimeMS()+waittime);
				q_set_retry->Bind(n + 1);
				q_set_retry->Bind(res[i]["id"]);
				q_set_retry->Write();
				q_set_retry->Reset();
			}
			else
			{
				q_remove_mail->Bind(res[i]["id"]);
				q_remove_mail->Write();
				q_remove_mail->Reset();
			}
		}
	}
}
