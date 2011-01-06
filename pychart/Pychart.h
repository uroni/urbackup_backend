#include "IPychart.h"
#include "../Interface/File.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"

#include <queue>

class Pychart : public IPychart
{
public:
	Pychart(void);

	void operator()(void);

	unsigned int drawGraph(const SChartInfo &ci);
	unsigned int drawPie(const SPieInfo &pi);
	unsigned int drawBar(const SBarInfo &pi);
	
	IFile* queryForFile(unsigned int id, int waitms=0);

private:
	IFile* drawGraphInt(const SChartInfo &ci);
	IFile* drawPieInt(const SPieInfo &pi);
	IFile* drawBarInt(const SBarInfo &pi);

	std::queue<std::pair<unsigned int,SChartInfo> > cis;
	std::queue<std::pair<unsigned int,SPieInfo> > pis;
	std::queue<std::pair<unsigned int,SBarInfo> > bis;
	std::vector<std::pair<unsigned int, IFile*> > rets;
	unsigned int curr_id;

	IMutex *mutex;
	ICondition *cond;
	ICondition *cond2;
};