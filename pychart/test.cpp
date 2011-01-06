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

#include "../Interface/Action.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "IPychartFactory.h"

namespace Actions
{
	ACTION(test_chart);
	ACTION(test_pie);
}

ACTION_IMPL(test_chart)
{
	Server->setContentType(tid, "image/png");
	str_map n;
	PLUGIN_ID pc_id=Server->StartPlugin("pychart", n);
	IPychartFactory *fak=(IPychartFactory*)Server->getPlugin(tid, pc_id);
	IPychart *pychart=fak->getPychart();

	SChartInfo ci;
	ci.dimensions=2;
	std::vector<float> xdata;
	std::vector<float> ydata;
	xdata.push_back(2);
	ydata.push_back(5);
	xdata.push_back(3);
	ydata.push_back(10);
	xdata.push_back(4);
	ydata.push_back(6);
	ci.data.push_back(xdata);
	ci.data.push_back(ydata);
	ci.style="r";
	unsigned int image_id=pychart->drawGraph(ci);
	IFile *out=pychart->queryForFile(image_id, -1);

	std::string fdata;
	fdata=out->Read((_u32)out->Size());
	Server->Write(tid, fdata, false);
	std::string fn=out->getFilename();
	Server->destroy(out);
	Server->deleteFile(fn);
}

ACTION_IMPL(test_pie)
{
	Server->setContentType(tid, "image/png");
	str_map n;
	PLUGIN_ID pc_id=Server->StartPlugin("pychart", n);
	IPychartFactory *fak=(IPychartFactory*)Server->getPlugin(tid, pc_id);
	IPychart *pychart=fak->getPychart();

	SPieInfo pi;
	std::vector<float> xdata;
	xdata.push_back(0.5);
	xdata.push_back(0.25);
	xdata.push_back(0.25);
	pi.data=xdata;	
	pi.labels.push_back("Frak1");
	pi.labels.push_back("Frak2");
	pi.labels.push_back("Frak3");
	pi.sizex=400;
	pi.sizey=400;
	unsigned int image_id=pychart->drawPie(pi);
	IFile *out=pychart->queryForFile(image_id, -1);

	std::string fdata;
	fdata=out->Read((_u32)out->Size());
	Server->Write(tid, fdata, false);
	std::string fn=out->getFilename();
	Server->destroy(out);
	Server->deleteFile(fn);
}