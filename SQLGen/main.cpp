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
		std::cout << "Usage: SQLGen [SQLite database filename] [cpp-file] ([Attached db name] [Attached db filename] ...)" << std::endl;
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

	for(int i=3;i+1<argc;i+=2)
	{
		if(!Server->attachToDatabase(argv[i+1], argv[i], maindb))
		{
			std::cout << "Could not attach database \"" << argv[i+1] << "\"" << std::endl;
		}
	}

	std::string cppfile_data=getFile(cppfile);
	std::string headerfile_data=getFile(headerfile);

	writestring(cppfile_data, cppfile+".sqlgenbackup");
	writestring(headerfile_data, headerfile+".sqlgenbackup");

	try
	{
		sqlgen(Server->getDatabase(Server->getThreadID(), maindb), cppfile_data, headerfile_data);
	}
	catch (std::exception& e)
	{
		std::cout << "Error: " << e.what() << std::endl;
		return 1;
	}

	writestring(cppfile_data, cppfile);
	writestring(headerfile_data, headerfile);

	std::cout << "SQLGen: Ok." << std::endl;

	return 0;
}