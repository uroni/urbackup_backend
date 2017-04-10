#pragma once

#include "../Interface/Thread.h"

class Alerts : public IThread
{
public:
	void operator()();
};