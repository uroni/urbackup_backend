#pragma once
#include <vector>
#include <stack>

namespace fileserv
{
	class CBufMgr
	{
	public:
		CBufMgr(unsigned int nbuf, unsigned int bsize);
		~CBufMgr(void);

		char* getBuffer(void);
		void releaseBuffer(char* buf);
		unsigned int nfreeBufffer(void);

	private:

		std::vector<char*> buffers;
		std::stack<char*> free_buffers;
	};
}




