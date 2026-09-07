#ifndef TREENODE_H
#define TREENODE_H

#include <string>
#include <vector>

#include "../../Interface/Types.h"

const size_t c_treenode_data_size_file=2*sizeof(int64);
const size_t c_treenode_data_size_dir=sizeof(int64);

class TreeNode
{
public:
	TreeNode(const char* name, const char* data, TreeNode *parent, char node_type);
	TreeNode(void);

	void setName(const char* pName);
	void setData(const char* pData);

	std::string getName();
	std::string getData();
	const char* getDataPtr();

	bool equals(const TreeNode& other);
	bool nameEquals(const TreeNode& other);
	int nameCompare(const TreeNode& other);
	bool dataEquals(const TreeNode& other);

	TreeNode* getFirstChild(void);
	void setNextSibling(TreeNode *pNextSibling);
	TreeNode *getNextSibling(void);
	void setHasChildren(void);
	TreeNode* getChild(size_t n);
	void setParent(TreeNode *pParent);
	TreeNode *getParent(void);

	void setType(char t);
	char getType();

	void setId(size_t pId);
	size_t getId(void) const;

	bool isMapped();
	void setMapped(bool b);

	void setSubtreeChanged(bool b);
	bool getSubtreeChanged();

	size_t getDataSize();

private:
	const char* name;
	const char* data;

	TreeNode *nextSibling;
	TreeNode *parent;

	size_t id;

	/* byte-sized members last: keeps the node at 48 bytes on 64-bit */
	char node_type;
	bool has_children;
	bool mapped;
	bool subtree_changed;
};


#endif //TREENODE_H