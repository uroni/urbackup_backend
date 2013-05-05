#include <iostream>
#include <string>
#define DEF_SERVER
#include "../Server.h"
#include "sqlgen.h"
#include "../stringtools.h"

CServer *Server;
DATABASE_ID maindb=0;
bool run=false;


int main(int argc, char* argv[])
{
	if(argc<3)
	{
		std::cout << "Usage: SQLGen [SQLite database filename] [cpp-file]" << std::endl;
		return 1;
	}

	Server=new CServer;
	Server->setup();

	std::string sqlite_db_str=argv[1];
	std::string cppfile=argv[2];
	std::string headerfile=getuntil(".cpp", cppfile)+".h";

	if(!Server->openDatabase(sqlite_db_str, maindb))
	{
		std::cout << "Could not open sqlite db \"" << sqlite_db_str << "\"" << std::endl;
	}

	std::string cppfile_data=getFile(cppfile);
	std::string headerfile_data=getFile(cppfile);

	sqlgen(Server->getDatabase(Server->getThreadID(), maindb), cppfile_data, headerfile_data);

	return 0;
}