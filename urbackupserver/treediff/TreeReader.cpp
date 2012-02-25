/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "TreeReader.h"
#include <fstream>
#include <iostream>
#include <memory.h>
#include <stack>
#include "../../stringtools.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../Interface/Server.h"

const size_t buffer_size=4096;

bool TreeReader::readTree(const std::string &fn)
{
	std::fstream in;
	in.open(fn.c_str(), std::ios::in | std::ios::binary );
	if(!in.is_open())
		return false;

	size_t read;
	char buffer[buffer_size];
	int state=0;
	size_t lines=0;
	do
	{
		in.read(buffer, buffer_size);
		read=(size_t)in.gcount();
		
		for(size_t i=0;i<read;++i)
		{
			const char ch=buffer[i];
			switch(state)
			{
				case 0:
					{
						if(ch=='d')
						{
							state=3;
						}
						else
						{
							state=1;
						}
					}break;
				case 1:
					{
						if(ch!='\r')
						{
							state=2;
						}
					}break;
				case 2:
					{
						if(ch=='\n')
						{
							++lines;
							state=0;
						}
					}break;
				case 3:
					{
						state=4;
					}break;
				case 4:
					{
						if(ch=='.')
							state=5;
						else
							state=10;
					}break;
				case 5:
					{
						if(ch=='.')
							state=6;
						else
							state=10;
					}break;
				case 6:
					{
						if(ch=='"')
							state=7;
						else
							state=10;
					}break;
				case 7:
					{
						if(ch=='\n')
						{
							state=0;
						}
					}break;
				case 10:
					{
						if(ch=='\n')
						{
							++lines;
							state=0;
						}
					}break;
			}		
		}
	}
	while(read>0);

	in.clear();
	in.seekg(0, std::ios::beg);


	
	
	state=0;
	bool isdir=false;
	std::string data;
	std::string name;
	std::stack<TreeNode*> parents;
	std::stack<TreeNode*> lastNodes;
	bool firstChild=true;

	size_t idx=1;
	nodes.resize(lines+1);

	nodes[0].setName("root");
	nodes[0].setData("d");

	parents.push(&nodes[0]);
	lastNodes.push(&nodes[0]);

	lines=0;


	do
	{
		in.read(buffer, buffer_size);
		read=(size_t)in.gcount();
		
		for(size_t i=0;i<read;++i)
		{
			const char ch=buffer[i];
			switch(state)
			{
				case 0:
					if(ch=='f')
					{
						data+='f';
					}
					else if(ch=='d')
					{
						data+='d';
						isdir=true;
					}
					else
					{
						Log("Error parsing file readTree - 1");
						return false;
					}

					state=1;
					break;
				case 1:
					//"
					state=2;
					break;
				case 2:
					if(ch=='"')
					{
						state=3;
					}
					else
					{
						name+=ch;
					}
					break;
				case 3:
					if(ch==' ')
					{
						state=4;
						break;
					}
					else
					{
						state=10;
					}
				case 4:
					if(state==4)
					{
						if(ch!='\n')
						{
							data+=ch;
							break;
						}
					}
				case 10:
					if(ch=='\n')
					{
						if(name!="..")
						{
							nodes[idx].setName(name);
							nodes[idx].setId(lines);

							char ch=data[0];
							if(ch=='f')
							{
								std::string sdata=data.substr(1);
								std::string filesize=getuntil(" ", sdata);
								std::string last_mod=getafter(" ", sdata);

								_i64 ifilesize=os_atoi64(filesize);
								_i64 ilast_mod=os_atoi64(last_mod);

								std::string ndata;
								ndata.resize(1+sizeof(_i64)*2);
								ndata[0]=ch;
								memcpy(&ndata[1], &ifilesize, sizeof(_i64));
								memcpy(&ndata[1+sizeof(_i64)], &ilast_mod, sizeof(_i64));
								nodes[idx].setData(ndata);
							}
							else
							{
								nodes[idx].setData(data);
							}

							if(firstChild)
							{
								lastNodes.push(&nodes[idx]);
								firstChild=false;
							}
							else
							{
								lastNodes.top()->setNextSibling(&nodes[idx]);
								lastNodes.pop();
								lastNodes.push(&nodes[idx]);
							}

							if(!parents.empty())
							{
								parents.top()->incrementNumChildren();
								nodes[idx].setParent(parents.top());
							}

							if(isdir)
							{
								parents.push(&nodes[idx]);
								firstChild=true;
							}

							++idx;
						}
						else
						{
							if(!parents.empty())
							{
								parents.pop();
							}
							else
							{
								Log("TreeReader: parents empty");
								return false;
							}
							if(!firstChild)
							{
								if(lastNodes.empty())
								{
									Log("TreeReader: lastNodes empty");
									return false;
								}
								lastNodes.top()->setNextSibling(NULL);
								lastNodes.pop();
							}
							firstChild=false;
						}

						name.clear();
						data.clear();
						isdir=false;
						state=0;
						++lines;
					}
			}		
		}
	}
	while(read==buffer_size);

	nodes[0].setNextSibling(NULL);

	return true;
}

void TreeReader::Log(const std::string &str)
{
	Server->Log(str, LL_ERROR);
}

std::vector<TreeNode> * TreeReader::getNodes(void)
{
	return &nodes;
}