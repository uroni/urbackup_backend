/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "../Interface/Thread.h"
#include "ThreadPool.h"
#include <windows.h>

#include <iostream>
#include <mutex>

using namespace std;

std::deque<int> rands;
std::mutex gmutex;

class Test1 : public IThread
{
public:
	void operator()(void)
	{
		std::cout << "Start\r\n";
		int s;

		{
			std::lock_guard<std::mutex> lock(gmutex);
			s=rands.front()%60000+2000;
			rands.erase( rands.begin() );
		}

		std::cout << "Sleeping " << s << " s "<< endl;
		Sleep(s);
		std::cout << "Hallo\r\n";
	}
};
	

/*int maintest()
{
	CThreadPool p;
	
	srand(0);
	for(unsigned int i=0;i<100;++i)
		rands.push_back(rand());

	Test1 test;
	std::vector<THREADPOOL_TICKET> tickets;
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	tickets.push_back(p.execute(&test));
	p.waitFor(tickets);

	std::cout << "done.\r\n";

	exit(5);
}*/

	