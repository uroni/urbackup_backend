/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
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

#include "TreeDiff.h"
#include "TreeReader.h"
#include <algorithm>

std::vector<size_t> TreeDiff::diffTrees(const std::string &t1, const std::string &t2, bool &error, std::vector<size_t> *deleted_ids)
{
	std::vector<size_t> ret;

	TreeReader r1;
	if(!r1.readTree(t1))
	{
		error=true;
		return ret;
	}

	TreeReader r2;
	if(!r2.readTree(t2))
	{
		error=true;
		return ret;
	}

	gatherDiffs(&(*r1.getNodes())[0], &(*r2.getNodes())[0], ret);
	if(deleted_ids!=NULL)
	{
		gatherDeletes(&(*r1.getNodes())[0], *deleted_ids);
		std::sort(deleted_ids->begin(), deleted_ids->end());
	}

	std::sort(ret.begin(), ret.end());

	return ret;
}

void TreeDiff::gatherDiffs(TreeNode *t1, TreeNode *t2, std::vector<size_t> &diffs)
{
	size_t nc_2=t2->getNumChildren();
	size_t nc_1=t1->getNumChildren();
	TreeNode *c2=t2->getFirstChild();
	while(c2!=NULL)
	{		
		bool found=false;
		TreeNode *c1=t1->getFirstChild();
		while(c1!=NULL)
		{
			if(c1!=NULL && c1->getName()==c2->getName() && c1->getData()==c2->getData() )
			{
				gatherDiffs(c1, c2, diffs);
				c2->setMappedNode(c1);
				c1->setMappedNode(c2);
				found=true;
				break;
			}
			c1=c1->getNextSibling();
		}

		if(!found)
		{
			diffs.push_back(c2->getId());
		}

		c2=c2->getNextSibling();
	}
}

void TreeDiff::gatherDeletes(TreeNode *t1, std::vector<size_t> &deleted_ids)
{
	TreeNode *c1=t1->getFirstChild();
	while(c1!=NULL)
	{
		if(c1->getMappedNode()==NULL)
		{
			deleted_ids.push_back(c1->getId());
		}
		gatherDeletes(c1, deleted_ids);
		c1=c1->getNextSibling();
	}
}