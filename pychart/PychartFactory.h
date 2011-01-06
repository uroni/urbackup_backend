#include "IPychartFactory.h"

class PychartFactory : public IPychartFactory
{
public:
	IPychart * getPychart(void);

	static void initializePychart(void);

private:

	static IPychart *pychart;
};