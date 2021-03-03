#pragma once

typedef struct _GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;

typedef GUID* LPGUID;
typedef GUID* PGUID;

#define DEFINE_GUID(gname, data1, data2, data3, data4a, data4b, data4c, data4d, data4e, data4f, data4g, data4h) static GUID gname = { data1, data2, data3, {data4a, data4b, data4c, data4d, data4e, data4f, data4g, data4h} };

DEFINE_GUID(GUID_DEVINTERFACE_VOLUME, 0x3eee337e, 0xd03b, 0x4980, 0xa8, 0xed, 0x8c, 0x40, 0x6d, 0x68, 0xf3, 0xf0);
DEFINE_GUID(GUID_DEVINTERFACE_HIDDEN_VOLUME, 0x3eee337e, 0xd03b, 0x4980, 0xa8, 0xed, 0x8c, 0x40, 0x6d, 0x68, 0xf3, 0xf1);
DEFINE_GUID(GUID_DEVINTERFACE_DISK, 0x3eee337e, 0xd03b, 0x4980, 0xa8, 0xed, 0x8c, 0x40, 0x6d, 0x68, 0xf3, 0xf2);

