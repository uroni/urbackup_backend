#pragma once
#include <string>
#include "../Interface/Thread.h"
#include "../Interface/Condition.h"

class Mailer : public IThread
{
public:
	static bool sendMail(const std::string& send_to, const std::string& subject, const std::string& message);

	static void init();

	void operator()();

private:
	static bool queue_limit;
	static bool queued_mail;
	static IMutex* mutex;
	static ICondition* cond;
};