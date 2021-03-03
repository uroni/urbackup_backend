#pragma once
#include "windef.h"

#ifdef __cplusplus
extern "C" {
#endif

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


#ifdef __cplusplus
}
#endif