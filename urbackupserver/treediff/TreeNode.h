#ifndef TREENODE_H
#define TREENODE_H

#include <string>
#include <vector>

class TreeNode
{
public:
	TreeNode(const std::string &name, const std::string &data, TreeNode *parent);
	TreeNode(void);

	void setName(const std::string &pName);
	void setData(const std::string &pData);

	const std::string& getName();
	const std::string& getData();

	size_t getNumChildren();
	TreeNode* getFirstChild(void);
	void setNextSibling(TreeNode *pNextSibling);
	TreeNode *getNextSibling(void);
	void incrementNumChildren(void);
	TreeNode* getChild(size_t n);
	void setParent(TreeNode *pParent);
	TreeNode *getParent(void);

	void setId(size_t pId);
	size_t getId(void) const;

	TreeNode *getMappedNode();
	void setMappedNode(TreeNode *pMappedNode);

	void setSubtreeChanged(bool b);
	bool getSubtreeChanged();

private:

	std::string name;
	std::string data;

	TreeNode *nextSibling;
	TreeNode *parent;
	TreeNode *mapped_node;
	size_t num_children;
	bool subtree_changed;

	size_t id;
};


#endif //TREENODE_H