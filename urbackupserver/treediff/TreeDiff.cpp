/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "TreeDiff.h"
#include "TreeReader.h"
#include <algorithm>

std::vector<size_t> TreeDiff::diffTrees(const std::string &t1, const std::string &t2, bool &error,
	std::vector<size_t> *deleted_ids, std::vector<size_t>* large_unchanged_subtrees,
	std::vector<size_t> *modified_inplace_ids, std::vector<size_t> &dir_diffs,
	std::vector<size_t> *deleted_inplace_ids)
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

	gatherDiffs(&(*r1.getNodes())[0], &(*r2.getNodes())[0], 0, ret, modified_inplace_ids, 
		dir_diffs, deleted_inplace_ids);
	if(deleted_ids!=NULL)
	{
		gatherDeletes(&(*r1.getNodes())[0], *deleted_ids);
		std::sort(deleted_ids->begin(), deleted_ids->end());
	}
	if(large_unchanged_subtrees!=NULL)
	{
		gatherLargeUnchangedSubtrees(&(*r2.getNodes())[0], *large_unchanged_subtrees);
		std::sort(large_unchanged_subtrees->begin(), large_unchanged_subtrees->end());
	}

	std::sort(ret.begin(), ret.end());
	std::sort(dir_diffs.begin(), dir_diffs.end());

	if(modified_inplace_ids!=NULL)
	{
		std::sort(modified_inplace_ids->begin(), modified_inplace_ids->end());
	}

	if (deleted_inplace_ids != NULL)
	{
		std::sort(deleted_inplace_ids->begin(), deleted_inplace_ids->end());
	}

	return ret;
}

void TreeDiff::gatherDiffs(TreeNode *t1, TreeNode *t2, size_t depth, std::vector<size_t> &diffs,
	std::vector<size_t> *modified_inplace_ids, std::vector<size_t> &dir_diffs,
	std::vector<size_t> *deleted_inplace_ids)
{
	size_t nc_2=t2->getNumChildren();
	size_t nc_1=t1->getNumChildren();
	TreeNode *c2=t2->getFirstChild();
	TreeNode *c1=t1->getFirstChild();
	bool did_subtree_change=false;
	while(c2!=NULL)
	{		
		int cmp = 1;
		if(c1!=NULL)
		{
			if (c1->getType() == 'f'
				&& c2->getType() == 'd')
			{
				cmp = -1;
			}
			else if (c1->getType() == 'd'
				&& c2->getType() == 'f')
			{
				cmp = 1;
			}
			else
			{
				cmp = c1->nameCompare(*c2);
			}
		}

		//root may be unsorted
		if (cmp != 0
			&& depth == 0)
		{
			TreeNode* sn = t1->getFirstChild();
			while (sn != NULL)
			{
				if (c2->getType() == sn->getType()
					&& c2->nameEquals(*sn)
					&& sn->getMappedNode()==NULL)
				{
					cmp = 0;
					c1 = sn;
					break;
				}
				sn = sn->getNextSibling();
			}
		}

		if(cmp==0)
		{
			bool equal_dir = (c1->getType()=='d' && c2->getType()=='d');
			bool data_equals = c1->dataEquals(*c2);

			if(equal_dir && !data_equals)
			{
				dir_diffs.push_back(c2->getId());
				subtreeChanged(c2);
			}

			if( equal_dir
				|| data_equals )
			{
				gatherDiffs(c1, c2, depth+1, diffs, modified_inplace_ids, 
					dir_diffs, deleted_inplace_ids);
				c2->setMappedNode(c1);
				c1->setMappedNode(c2);
			}
			else
			{
				if( modified_inplace_ids!=NULL
					&& c1->getType() == c2->getType() )
				{
					modified_inplace_ids->push_back(c2->getId());
				}

				if (deleted_inplace_ids != NULL
					&& c1->getType() == c2->getType())
				{
					deleted_inplace_ids->push_back(c1->getId());
				}
				
				diffs.push_back(c2->getId());
				subtreeChanged(c2);				
			}

#ifndef _WIN32
			/**
			* Stop it from moving directories above symlinks to the directory link
			* pool on Linux/FreeBSD, as then symbolic links within that directory
			* would not be able to point to the current backup (symbolic links
			* are relative to the symbolic link location)
			* On Windows this works. Could be because it uses junctions for the
			* symlinks to the directory pool.
			**/
			const int64* dataptr = reinterpret_cast<const int64*>(c2->getDataPtr());
			int64 change_indicator = 0;
			if (c2->getType() == 'd'
				&& c2->getDataSize() == sizeof(int64))
			{
				change_indicator = *dataptr;
			}
			else if (c2->getType() == 'f'
				&& c2->getDataSize() == 2 * sizeof(int64))
			{
				change_indicator = *(dataptr + 1);
			}

			const int64 symlink_mask = 0x7000000000000000LL;
			if ( (change_indicator>0 || c2->getType() == 'd')
				&& (change_indicator & symlink_mask)>0 )
			{
				subtreeChanged(c2);
			}
#endif

			c1=c1->getNextSibling();
			c2=c2->getNextSibling();
		}
		else if(cmp<0)
		{
			c1=c1->getNextSibling();
		}
		else
		{
			diffs.push_back(c2->getId());
			subtreeChanged(c2);

			c2=c2->getNextSibling();
		}
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

void TreeDiff::subtreeChanged(TreeNode* t2)
{
	TreeNode* p = t2->getParent();
	if(p==NULL) return;

	do
	{
		if(p->getSubtreeChanged())
		{
			return;
		}

		p->setSubtreeChanged(true);
		p = p->getParent();
	}
	while(p!=NULL);
}

void TreeDiff::gatherLargeUnchangedSubtrees( TreeNode *t2, std::vector<size_t> &large_unchanged_subtrees )
{
	TreeNode *c2=t2->getFirstChild();
	while(c2!=NULL)
	{
		if(!c2->getSubtreeChanged()
			&& c2->getMappedNode()!=NULL
			&& getTreesize(c2,10)>10)
		{
			large_unchanged_subtrees.push_back(c2->getId());
		}
		else
		{
			gatherLargeUnchangedSubtrees(c2, large_unchanged_subtrees);
		}
		c2=c2->getNextSibling();
	}
}

size_t TreeDiff::getTreesize( TreeNode* t, size_t limit )
{
	size_t treesize=1;
	TreeNode *c=t->getFirstChild();
	while(c!=NULL)
	{
		treesize+=getTreesize(c, limit);
		if(treesize>limit)
		{
			return treesize;
		}
		c=c->getNextSibling();
	}
	return treesize;
}
