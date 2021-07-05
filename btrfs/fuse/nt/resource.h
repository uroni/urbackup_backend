#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "windef.h"

	struct ERESOURCE_INT;

	typedef struct _ERESOURCE {
		struct ERESOURCE_INT* res;
	} ERESOURCE;

	typedef ERESOURCE* PERESOURCE;


	BOOLEAN ExAcquireResourceSharedLite(PERESOURCE Resource, BOOLEAN Wait);

	BOOLEAN ExAcquireResourceExclusiveLite(PERESOURCE Resource, BOOLEAN Wait);

	BOOLEAN ExReleaseResourceLite(PERESOURCE Resource);

	BOOLEAN ExDeleteResourceLite(PERESOURCE Resource);

	BOOLEAN ExInitializeResourceLite(PERESOURCE Resource);

	void ExConvertExclusiveToSharedLite(PERESOURCE Res);

	BOOLEAN ExIsResourceAcquiredSharedLite(PERESOURCE Resource);

	BOOLEAN ExIsResourceAcquiredExclusive(PERESOURCE Resource);

	BOOLEAN ExIsResourceAcquiredExclusiveLite(PERESOURCE Resource);

	void ExReleaseResource(PERESOURCE Resource);
#ifdef __cplusplus
}
#endif