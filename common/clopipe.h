#pragma once

#include <fcntl.h>

#if defined(O_CLOEXEC) && !defined(__APPLE__) && !defined(__FreeBSD__)
#define clopipe(x) pipe2(x, O_CLOEXEC)
#else
namespace
{
	int clopipe(int* x)
	{
		int rc = pipe(x);
		if (rc != 0) return rc;
		fcntl(x[0], F_SETFD, FD_CLOEXEC);
		fcntl(x[1], F_SETFD, FD_CLOEXEC);
		return rc;
	}
}
#endif