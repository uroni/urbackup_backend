#ifndef IPYCHART_H
#define IPYCHART_H

#include <vector>
#include <string>
#include "../Interface/Thread.h"

enum PltFormat
{
	pltf_png,
	pltf_svg
};


class IFile;

struct SChartInfo
{
	SChartInfo(): dimensions(2), format(pltf_png), sizex(640), sizey(480) {}

	int dimensions;
	std::vector< std::vector<float> > data;
	std::string xlabel;
	std::string ylabel;
	std::string title;
	std::string style;
	PltFormat format;

	int sizex;
	int sizey;
};

struct SPieInfo
{
	SPieInfo():shadow(false), format(pltf_png), sizex(640), sizey(480) {}
	std::vector<float> data;
	std::string title;
	std::vector<std::string> labels;
	std::string colors;
	bool shadow;

	PltFormat format;
	int sizex;
	int sizey;
};

struct SBarInfo
{
	SBarInfo(): format(pltf_png), sizex(640), sizey(480), barwidth(0.25f) {}
	std::vector<float> data;
	std::string title;
	std::vector<std::string> xlabels;
	std::string ylabel;
	std::string color;
	float barwidth;

	PltFormat format;
	int sizex;
	int sizey;
};

class IPychart : public IThread
{
public:
	virtual unsigned int drawGraph(const SChartInfo &ci)=0;
	virtual unsigned int drawPie(const SPieInfo &pi)=0;
	virtual unsigned int drawBar(const SBarInfo &pi)=0;

	virtual IFile* queryForFile(unsigned int id, int waitms=0)=0;
};

#endif //IPYCHART_H