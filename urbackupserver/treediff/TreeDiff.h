#include <string>
#include <vector>

class TreeNode;

class TreeDiff
{
public:
	static std::vector<size_t> diffTrees(const std::string &t1, const std::string &t2, bool &error);

private:
	static void gatherDiffs(TreeNode *t1, TreeNode *t2, std::vector<size_t> &diffs);
};