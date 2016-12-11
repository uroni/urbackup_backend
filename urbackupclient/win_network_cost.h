#pragma once
#include <Windows.h>
#include <string>
#include <wlanapi.h>
#include <wcmapi.h>

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "wcmapi.lib")


bool AllCurrentConnectionsMetered(bool& metered)
{
	HANDLE hWlan;
	DWORD version;
	if (WlanOpenHandle(2, NULL, &version, &hWlan) != ERROR_SUCCESS)
	{
		return false;
	}

	PWLAN_INTERFACE_INFO_LIST interface_list;
	if (WlanEnumInterfaces(hWlan, NULL, &interface_list) != ERROR_SUCCESS)
	{
		WlanCloseHandle(hWlan, NULL);
		return false;
	}

	bool ret = true;
	metered = false;

	bool first_connection = true;

	for (DWORD i = 0; i < interface_list->dwNumberOfItems; ++i)
	{
		WLAN_INTERFACE_INFO& info = interface_list->InterfaceInfo[i];

		if (info.isState == wlan_interface_state_connected
			|| info.isState == wlan_interface_state_ad_hoc_network_formed)
		{
			PWLAN_CONNECTION_ATTRIBUTES connection_attrs = NULL;
			DWORD connection_attrs_size = 0;
			if(WlanQueryInterface(hWlan, &info.InterfaceGuid,
				wlan_intf_opcode_current_connection, NULL,
				&connection_attrs_size, 
				reinterpret_cast<PVOID*>(&connection_attrs), NULL)!=ERROR_SUCCESS)
			{
				ret = false;
				continue;
			}

			PWCM_CONNECTION_COST_DATA connection_cost_data = NULL;
			DWORD connection_cost_size = 0;
			if (WcmQueryProperty(&info.InterfaceGuid, connection_attrs->strProfileName,
				wcm_intf_property_connection_cost, NULL,
				&connection_cost_size,
				reinterpret_cast<PBYTE*>(&connection_cost_data)) != ERROR_SUCCESS)
			{
				WlanFreeMemory(connection_attrs);
				ret = false;
				continue;
			}

			bool curr_metered = (connection_cost_data->ConnectionCost
					& (WCM_CONNECTION_COST_FIXED 
						| WCM_CONNECTION_COST_VARIABLE) )>0;

			if (first_connection)
			{
				metered = curr_metered;
				first_connection = false;
			}
			else if(metered)
			{
				metered = curr_metered;
			}

			WlanFreeMemory(connection_attrs);
			WlanFreeMemory(connection_cost_data);

			ret = true;
		}
	}

	WlanFreeMemory(interface_list);
	WlanCloseHandle(hWlan, NULL);

	return ret;
}