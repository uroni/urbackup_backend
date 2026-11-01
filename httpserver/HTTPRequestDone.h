#pragma once

#include <atomic>
#include "../Interface/CustomClient.h"

// Lets a request handler running in the thread pool tell the HTTP client
// that it is done, so the service worker finishes the connection right away
// instead of at its next timeout
class CHTTPRequestDone
{
public:
	CHTTPRequestDone()
		: wakeup(NULL), done(false) {}

	void reset(const IClientWakeup* p_wakeup)
	{
		wakeup = p_wakeup;
		done = false;
	}

	void notify()
	{
		done = true;
		if (wakeup != NULL)
		{
			wakeup->wakeup();
		}
	}

	bool isDone() const
	{
		return done;
	}

	// Notifies when the handler returns, including early returns
	class Guard
	{
	public:
		Guard(CHTTPRequestDone* request_done)
			: request_done(request_done) {}

		~Guard()
		{
			if (request_done != NULL)
			{
				request_done->notify();
			}
		}

	private:
		CHTTPRequestDone* request_done;
	};

private:
	const IClientWakeup* wakeup;
	std::atomic<bool> done;
};
