#ifndef TREENODE_H
#define TREENODE_H

#include <string>
#include <vector>

#include "../../Interface/Types.h"

const size_t c_treenode_data_size=2*sizeof(int64);

class TreeNode
{
public:
	TreeNode(const char* name, const char* data, TreeNode *parent);
	TreeNode(void);

	void setName(const char* pName);
	void setData(const char* pData);

	std::string getName();
	std::string getData();

	bool equals(const TreeNode& other);
	bool nameEquals(const TreeNode& other);
	bool dataEquals(const TreeNode& other);

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

	const char* name;
	const char* data;

	TreeNode *nextSibling;
	TreeNode *parent;
	TreeNode *mapped_node;
	size_t num_children;
	bool subtree_changed;

	size_t id;
};


#endif //TREENODE_H