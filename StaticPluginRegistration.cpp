#include "StaticPluginRegistration.h"


std::vector<SStaticPlugin>& get_static_plugin_registrations()
{
	static std::vector<SStaticPlugin> registrations;
	return registrations;
}



void register_static_plugin( LOADACTIONS loadaction, UNLOADACTIONS unloadaction, int prio)
{
	SStaticPlugin static_plugin = {loadaction, unloadaction, prio};
	get_static_plugin_registrations().push_back(static_plugin);
}

