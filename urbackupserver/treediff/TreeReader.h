#include "TreeNode.h"

class IFile;

class TreeReader
{
public:
	bool readTree(IFile* f);

	std::vector<TreeNode> * getNodes(void);
private:

	void Log(const std::string &str);

	std::vector<TreeNode> nodes;
	std::vector<char> stringbuffer;
};