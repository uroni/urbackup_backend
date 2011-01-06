__int64 unix_timestamp(SYSTEMTIME *sysTime)
{
    SYSTEMTIME unixTime;
    FILETIME unixTime2;
    FILETIME sysTime2;
    __int64 unixTime3;
    __int64 sysTime3;

    // Unix-Timestamp starts January 1 1970 00:00:00
    unixTime.wDay=1;
    unixTime.wDayOfWeek=4;
    unixTime.wHour=0;
    unixTime.wMilliseconds=0;
    unixTime.wMinute=0;
    unixTime.wMonth=1;
    unixTime.wSecond=0;
    unixTime.wYear=1970;
 
    SystemTimeToFileTime(&unixTime,&unixTime2);
    SystemTimeToFileTime(sysTime,&sysTime2);

    unixTime3=((ULARGE_INTEGER*)&unixTime2)->QuadPart;
    sysTime3=((ULARGE_INTEGER*)&sysTime2)->QuadPart;
 
    unixTime3=unixTime3/10000000;
    sysTime3=sysTime3/10000000;

    return (sysTime3-unixTime3);
}
