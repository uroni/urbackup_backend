#ifndef QUERY_H
#define QUERY_H

#include "Types.h"

class IQuery
{
public:
	virtual void Bind(const std::string &str)=0;
	virtual void Bind(const std::wstring &str)=0;
	virtual void Bind(unsigned int p)=0;
	virtual void Bind(int p)=0;
	virtual void Bind(double p)=0;
	virtual void Bind(int64 p)=0;
#ifdef _WIN64
	virtual void Bind(size_t p)=0;
#endif
	virtual void Bind(const char* buffer, _u32 bsize)=0;

	virtual void Reset(void)=0;

	virtual bool Write(void)=0;
	virtual db_nresults ReadN(void)=0;
	virtual db_results Read(void)=0;
};


#endif

