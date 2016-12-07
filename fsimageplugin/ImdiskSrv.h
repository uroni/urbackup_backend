#pragma once

#include "../Interface/Thread.h"

class ImdiskSrv : public IThread
{
public:
	ImdiskSrv();

	static bool installed();

	void operator()();
};