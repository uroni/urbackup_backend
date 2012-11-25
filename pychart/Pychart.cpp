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

#include "Pychart.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

#include <boost/python.hpp>

using namespace boost::python;

std::string EscapeStringForPython(const std::string &str)
{
	std::string ret;
	for(size_t i=0;i<str.size();++i)
	{
		if(str[i]=='\\' || str[i]=='"' )
			ret+="\\";

		ret+=str[i];
	}
	return ret;
}

std::string Vec2Array(std::vector<float> vec)
{
	std::string r="[";
	for(size_t i=0;i<vec.size();++i)
	{
		r+=nconvert(vec[i]);
		if(i+1<vec.size())
			r+=",";
	}
	r+="]";
	return r;
}

std::string Vec2Array(std::vector< std::vector<float> > vec)
{
	std::string r="[";
	for(size_t i=0;i<vec.size();++i)
	{
		r+=Vec2Array(vec[i]);
		if(i+1<vec.size())
			r+=",";
	}
	r+="]";
	return r;
}

std::string String2Array(std::vector<std::string> sa)
{
	std::string r="[";
	for(size_t i=0;i<sa.size();++i)
	{
		r+="\""+sa[i]+"\"";
		if(i+1<sa.size())
			r+=",";
	}
	r+="]";
	return r;
}

std::string format2String(PltFormat frm)
{
	switch(frm)
	{
	case pltf_png: return "png";
	case pltf_svg: return "svg";
	default: return "png";
	}
}

std::string cBool(bool b)
{
	if(b) return "True";
	else  return "False";
}

Pychart::Pychart(void)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();
	cond2=Server->createCondition();
	curr_id=0;
}

void Pychart::operator()(void)
{
	IScopedLock lock(mutex);
	
	static int t_num=0;

	if(t_num==0)
	{
#ifdef _WIN32
#ifndef _DEBUG
		std::string *prg_name=new std::string;
		*prg_name=Server->ConvertToUTF8(Server->getServerWorkingDir());
		Py_SetPythonHome((char*)prg_name->c_str());
#endif
#endif
		Py_Initialize();
	}
	else
	{
		Py_NewInterpreter();
	}

	++t_num;
	
	try
	{
		object main_module = import("__main__");
		object main_namespace = main_module.attr("__dict__");

		object ignored = exec_file("pychart/pychart.py", main_namespace);
	}
	catch(error_already_set const &)
	{
		Server->Log("Error while inizializing Python occured. Did you install numpy and matplotlib?", LL_ERROR);
		PyErr_Print();
	}

	while(true)
	{
		if(cis.empty() && bis.empty() && pis.empty())
			cond->wait(&lock);

		while(!cis.empty())
		{
			std::pair<unsigned int,SChartInfo> ci=cis.front();
			cis.pop();
			lock.relock(NULL);
			IFile *r=drawGraphInt(ci.second);
			lock.relock(mutex);
			rets.push_back(std::pair<unsigned int, IFile*>(ci.first, r));
			cond2->notify_all();
		}

		while(!pis.empty())
		{
			std::pair<unsigned int,SPieInfo> pi=pis.front();
			pis.pop();
			lock.relock(NULL);
			IFile *r=drawPieInt(pi.second);
			lock.relock(mutex);
			rets.push_back(std::pair<unsigned int, IFile*>(pi.first, r));
			cond2->notify_all();
		}

		while(!bis.empty())
		{
			std::pair<unsigned int,SBarInfo> bi=bis.front();
			bis.pop();
			lock.relock(NULL);
			IFile *r=drawBarInt(bi.second);
			lock.relock(mutex);
			rets.push_back(std::pair<unsigned int, IFile*>(bi.first, r));
			cond2->notify_all();
		}
	}
}

IFile* Pychart::drawGraphInt(const SChartInfo &ci)
{
	object main_module = import("__main__");
	object main_namespace = main_module.attr("__dict__");

	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL) return NULL;
	std::string tfn=tmp->getFilename();
	Server->destroy(tmp);

	std::string cmd="drawGraph(\""+EscapeStringForPython(tfn)+"\","+nconvert(ci.dimensions)+","+Vec2Array(ci.data)
					+",\""+ci.xlabel+"\",\""+ci.ylabel+"\",\""+ci.title+"\",\""+ci.style
					+"\",\""+format2String(ci.format)+"\","+nconvert(ci.sizex)+","
					+nconvert(ci.sizey)+")";
	try
	{
		object ignored = exec(cmd.c_str(), main_namespace);
	}
	catch(error_already_set const &)
	{
		PyErr_Print();
	}


	return Server->openFile(tfn, MODE_READ);
}

IFile* Pychart::drawPieInt(const SPieInfo &pi)
{
	object main_module = import("__main__");
	object main_namespace = main_module.attr("__dict__");

	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL) return NULL;
	std::string tfn=tmp->getFilename();
	Server->destroy(tmp);

	std::string cmd="drawPie(\""+EscapeStringForPython(tfn)+"\","+Vec2Array(pi.data)+",\""+pi.title+"\","+String2Array(pi.labels)+",\""
		+pi.colors+"\","+cBool(pi.shadow)+",\""+format2String(pi.format)+"\","+nconvert(pi.sizex)+","
					+nconvert(pi.sizey)+")";

	try
	{
		object ignored = exec(cmd.c_str(), main_namespace);
	}
	catch(error_already_set const &)
	{
		PyErr_Print();
	}

	return Server->openFile(tfn, MODE_READ);
}

IFile* Pychart::drawBarInt(const SBarInfo &pi)
{
	object main_module = import("__main__");
	object main_namespace = main_module.attr("__dict__");

	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL) return NULL;
	std::string tfn=tmp->getFilename();
	Server->destroy(tmp);

	std::string cmd="drawBar(\""+EscapeStringForPython(tfn)+"\","+Vec2Array(pi.data)+","+String2Array(pi.xlabels)+",\""+pi.ylabel+"\",\""+pi.title+"\",\""+pi.color+"\",\""
					+format2String(pi.format)+"\","+nconvert(pi.sizex)+","
					+nconvert(pi.sizey)+","+nconvert(pi.barwidth)+")";

	try
	{
		object ignored = exec(cmd.c_str(), main_namespace);
	}
	catch(error_already_set const &)
	{
		PyErr_Print();
	}

	return Server->openFile(tfn, MODE_READ);
}

unsigned int Pychart::drawGraph(const SChartInfo &ci)
{
	IScopedLock lock(mutex);
	++curr_id;
	unsigned int t_id=curr_id;
	cis.push(std::pair<unsigned int, SChartInfo>(t_id,ci));
	cond->notify_all();
	return t_id;
}

unsigned int Pychart::drawPie(const SPieInfo &pi)
{
	IScopedLock lock(mutex);
	++curr_id;
	unsigned int t_id=curr_id;
	pis.push(std::pair<unsigned int, SPieInfo>(t_id,pi));
	cond->notify_all();
	return t_id;
}

unsigned int Pychart::drawBar(const SBarInfo &pi)
{
	IScopedLock lock(mutex);
	++curr_id;
	unsigned int t_id=curr_id;
	bis.push(std::pair<unsigned int, SBarInfo>(t_id,pi));
	cond->notify_all();
	return t_id;
}

IFile* Pychart::queryForFile(unsigned int id, int waitms)
{
	IScopedLock lock(mutex);
	if(waitms>0)
		cond2->wait(&lock, waitms);
	else if(waitms<0)
		cond2->wait(&lock);
	
	for(size_t i=0;i<rets.size();++i)
	{
		if(rets[i].first==id)
		{
			IFile *rf=rets[i].second;
			rets.erase(rets.begin()+i);
			return rf;
		}
	}
	
	return NULL;
}