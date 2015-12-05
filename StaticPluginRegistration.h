#pragma once
#include <vector>

class IServer;

typedef void(*LOADACTIONS)(IServer*);
typedef void(*UNLOADACTIONS)(void);

void register_static_plugin(LOADACTIONS loadaction, UNLOADACTIONS unloadaction, int prio);

struct SStaticPlugin
{
	LOADACTIONS loadactions;
	UNLOADACTIONS unloadactions;
	int prio;

	bool operator<(const SStaticPlugin& other) const
	{
		return prio<other.prio;
	}
};

std::vector<SStaticPlugin>& get_static_plugin_registrations();

class RegisterPluginHelper
{
public:
	RegisterPluginHelper(LOADACTIONS loadaction, UNLOADACTIONS unloadaction, int prio)
	{
		register_static_plugin(loadaction, unloadaction, prio);
	}
};