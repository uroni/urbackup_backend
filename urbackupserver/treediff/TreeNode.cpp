/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "TreeNode.h"

TreeNode::TreeNode(const std::string &name, const std::string &data, TreeNode *parent)
	: name(name), data(data), parent(parent), num_children(0), nextSibling(NULL), mapped_node(NULL)
{
}

TreeNode::TreeNode(void)
	: num_children(0), nextSibling(NULL), mapped_node(NULL)
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

TreeNode *TreeNode::getMappedNode()
{
	return mapped_node;
}

void TreeNode::setMappedNode(TreeNode *pMappedNode)
{
	mapped_node=pMappedNode;
}
