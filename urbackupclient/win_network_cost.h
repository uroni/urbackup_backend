#pragma once
#include <Windows.h>
#include <string>
#include <wlanapi.h>
#include <wcmapi.h>

namespace
{
	typedef DWORD
		(WINAPI *td_WlanOpenHandle)(
			__in DWORD dwClientVersion,
			__reserved PVOID pReserved,
			__out PDWORD pdwNegotiatedVersion,
			__out PHANDLE phClientHandle
		);

	typedef DWORD (WINAPI
		*td_WlanEnumInterfaces)(
			_In_ HANDLE hClientHandle,
			_Reserved_ PVOID pReserved,
			_Outptr_ PWLAN_INTERFACE_INFO_LIST *ppInterfaceList
		);

	typedef DWORD (WINAPI
		*td_WlanCloseHandle)(
			_In_ HANDLE hClientHandle,
			_Reserved_ PVOID pReserved
		);

	typedef DWORD (WINAPI
		*td_WlanQueryInterface)(
			_In_ HANDLE hClientHandle,
			_In_ CONST GUID *pInterfaceGuid,
			_In_ WLAN_INTF_OPCODE OpCode,
			_Reserved_ PVOID pReserved,
			_Out_ PDWORD pdwDataSize,
			_Outptr_result_bytebuffer_(*pdwDataSize) PVOID *ppData,
			_Out_opt_ PWLAN_OPCODE_VALUE_TYPE pWlanOpcodeValueType
		);

	typedef DWORD
		(WINAPI
		*td_WcmQueryProperty)(
			_In_opt_ const GUID* pInterface,
			_In_opt_ LPCWSTR strProfileName,
			_In_ WCM_PROPERTY Property,
			_Reserved_ PVOID pReserved,
			_Out_ PDWORD pdwDataSize,
			_Outptr_result_buffer_maybenull_(*pdwDataSize) PBYTE *ppData
		);

	typedef VOID (WINAPI
		*td_WlanFreeMemory)(
			_In_ PVOID pMemory
		);

	td_WlanOpenHandle ptr_WlanOpenHandle = NULL;
	td_WlanEnumInterfaces ptr_WlanEnumInterfaces = NULL;
	td_WlanCloseHandle ptr_WlanCloseHandle = NULL;
	td_WlanQueryInterface ptr_WlanQueryInterface = NULL;
	td_WcmQueryProperty ptr_WcmQueryProperty = NULL;
	td_WlanFreeMemory ptr_WlanFreeMemory = NULL;
	bool wlanapi_init = false;

	bool load_wlanapi_ok()
	{
		return ptr_WlanOpenHandle != NULL
			&& ptr_WlanEnumInterfaces != NULL
			&& ptr_WlanCloseHandle != NULL
			&& ptr_WlanQueryInterface != NULL
			&& ptr_WcmQueryProperty != NULL
			&& ptr_WlanFreeMemory != NULL;
	}

	bool load_wlanapi()
	{
		if (wlanapi_init)
		{
			return load_wlanapi_ok();
		}

		wlanapi_init = true;

		HMODULE hWlanapi = LoadLibraryW(L"wlanapi.dll");
		if (hWlanapi == NULL)
		{
			return false;
		}

		HMODULE hWcmapi = LoadLibraryW(L"wcmapi.dll");
		if (hWcmapi == NULL)
		{
			return false;
		}

#define LOAD_FUNC(hmodule, name) ptr_ ## name = reinterpret_cast<td_ ## name>(GetProcAddress(hmodule, #name))

		LOAD_FUNC(hWlanapi, WlanOpenHandle);
		LOAD_FUNC(hWlanapi, WlanCloseHandle);
		LOAD_FUNC(hWlanapi, WlanEnumInterfaces);
		LOAD_FUNC(hWlanapi, WlanQueryInterface);
		LOAD_FUNC(hWcmapi, WcmQueryProperty);
		LOAD_FUNC(hWlanapi, WlanFreeMemory);

#undef LOAD_FUNC

		return load_wlanapi_ok();
	}


	bool AllCurrentConnectionsMetered(bool& metered)
	{
		if (!load_wlanapi())
		{
			return false;
		}

		HANDLE hWlan;
		DWORD version;
		DWORD rc = ptr_WlanOpenHandle(2, NULL, &version, &hWlan);
		if (rc != ERROR_SUCCESS)
		{
			return false;
		}

		PWLAN_INTERFACE_INFO_LIST interface_list;
		if (ptr_WlanEnumInterfaces(hWlan, NULL, &interface_list) != ERROR_SUCCESS)
		{
			ptr_WlanCloseHandle(hWlan, NULL);
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
				if (ptr_WlanQueryInterface(hWlan, &info.InterfaceGuid,
					wlan_intf_opcode_current_connection, NULL,
					&connection_attrs_size,
					reinterpret_cast<PVOID*>(&connection_attrs), NULL) != ERROR_SUCCESS)
				{
					ret = false;
					continue;
				}

				PWCM_CONNECTION_COST_DATA connection_cost_data = NULL;
				DWORD connection_cost_size = 0;
				if (ptr_WcmQueryProperty(&info.InterfaceGuid, connection_attrs->strProfileName,
					wcm_intf_property_connection_cost, NULL,
					&connection_cost_size,
					reinterpret_cast<PBYTE*>(&connection_cost_data)) != ERROR_SUCCESS)
				{
					ptr_WlanFreeMemory(connection_attrs);
					ret = false;
					continue;
				}

				bool curr_metered = (connection_cost_data->ConnectionCost
					& (WCM_CONNECTION_COST_FIXED
						| WCM_CONNECTION_COST_VARIABLE))>0;

				if (first_connection)
				{
					metered = curr_metered;
					first_connection = false;
				}
				else if (metered)
				{
					metered = curr_metered;
				}

				ptr_WlanFreeMemory(connection_attrs);
				ptr_WlanFreeMemory(connection_cost_data);

				ret = true;
			}
		}

		ptr_WlanFreeMemory(interface_list);
		ptr_WlanCloseHandle(hWlan, NULL);

		return ret;
	}
}