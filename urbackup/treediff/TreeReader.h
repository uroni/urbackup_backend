#include "TreeNode.h"

class TreeReader
{
public:
	bool readTree(const std::string &fn);

	std::vector<TreeNode> * getNodes(void);
private:

	void Log(const std::string &str);

	std::vector<TreeNode> nodes;
};