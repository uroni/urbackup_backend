#include "SharedMutex_boost.h"


ILock* SharedMutex::readLock()
{
	return new ReadLock(new boost::shared_lock<boost::shared_mutex>(mutex));
}

ILock* SharedMutex::writeLock()
{
	return new WriteLock(new boost::unique_lock<boost::shared_mutex>(mutex));
}


ReadLock::ReadLock( boost::shared_lock<boost::shared_mutex>* read_lock )
	: read_lock(read_lock)
{

}

WriteLock::WriteLock( boost::unique_lock<boost::shared_mutex>* write_lock )
	: write_lock(write_lock)
{

}
