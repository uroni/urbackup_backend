#ifndef IACTION_H
#define IACTION_H

#include <map>
#include <string>

#include "Types.h"
#include "Object.h"

#define ACTION(x) class x : public IAction\
	{public: virtual void Execute(str_map &GET, str_map &POST, THREAD_ID tid, str_nmap &PARAMS); virtual std::wstring getName(void);};
#define ACTION_IMPL(x) std::wstring Actions::x::getName(void){ return L ## #x; }\
						void Actions::x::Execute(str_map &GET, str_map &POST, THREAD_ID tid, str_nmap &PARAMS)

class IAction : public IObject
{
public:
	virtual void Execute(str_map &GET, str_map &POST, THREAD_ID tid, str_nmap &PARAMS)=0;
	virtual std::wstring getName(void)=0;
};

#endif //IACTION_H