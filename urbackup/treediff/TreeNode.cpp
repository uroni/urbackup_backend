#include "TreeNode.h"

TreeNode::TreeNode(const std::string &name, const std::string &data, TreeNode *parent) : name(name), data(data), parent(parent), num_children(0), nextSibling(NULL)
{
}

TreeNode::TreeNode(void) : num_children(0), nextSibling(NULL)
{
}

const std::string& TreeNode::getName()
{
	return name;
}

const std::string& TreeNode::getData()
{
	return data;
}

void TreeNode::setName(const std::string &pName)
{
	name=pName;
}

void TreeNode::setData(const std::string &pData)
{
	data=pData;
}

size_t TreeNode::getNumChildren()
{
	return num_children;
}

TreeNode* TreeNode::getFirstChild(void)
{
	if(num_children>0)
	{
		return this+1;
	}
	else
	{
		return NULL;
	}
}

void TreeNode::setNextSibling(TreeNode *pNextSibling)
{
	nextSibling=pNextSibling;
}

void TreeNode::incrementNumChildren(void)
{
	++num_children;
}

void TreeNode::setId(size_t pId)
{
	id=pId;
}

size_t TreeNode::getId(void) const
{
	return id;
}

TreeNode *TreeNode::getNextSibling(void)
{
	return nextSibling;
}

TreeNode* TreeNode::getChild(size_t n)
{
	TreeNode *firstChild=getFirstChild();
	size_t i=0;
	while(firstChild!=NULL && i!=n)
	{
		firstChild=firstChild->getNextSibling();
		++i;
	}
	if(i==n)
	{
		return firstChild;
	}
	else
	{
		return NULL;
	}
}

void TreeNode::setParent(TreeNode *pParent)
{
	parent=pParent;
}

TreeNode *TreeNode::getParent(void)
{
	return parent;
}