#pragma once

#include "../Interface/Thread.h"

class ImdiskSrv : public IThread
{
public:
	ImdiskSrv();

	void operator()();
};