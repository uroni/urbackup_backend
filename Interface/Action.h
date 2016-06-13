#ifndef IACTION_H
#define IACTION_H

#include <map>
#include <string>

#include "Types.h"
#include "Object.h"

#define ACTION(x) class x : public IAction\
	{public: virtual void Execute(str_map &GET, str_map &POST, THREAD_ID tid, str_map &PARAMS); virtual std::string getName(void);};
#define ACTION_IMPL(x) std::string Actions::x::getName(void){ return #x; }\
						void Actions::x::Execute(str_map &GET, str_map &POST, THREAD_ID tid, str_map &PARAMS)

class IAction : public IObject
{
public:
	virtual void Execute(str_map &GET, str_map &POST, THREAD_ID tid, str_map &PARAMS)=0;
	virtual std::string getName(void)=0;
};

#endif //IACTION_H