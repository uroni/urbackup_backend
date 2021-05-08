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

#pragma once
#include <list>
#include <map>
#include <stddef.h>

#include "../Interface/Mutex.h"

namespace common
{

template<typename K, typename V>
class lrucache
{
public:
	typedef std::list<std::pair<K const *, V> > list_t;
	typedef std::map<K, typename list_t::iterator> map_t;

	lrucache()
	{}

	void put(const K& key, const V& v)
	{
		typename map_t::iterator it=lru_access.find(key);

		if(it!=lru_access.end())
		{
			bring_to_front(it);
			it->second->second=v;
		}
		else
		{
			typename map_t::iterator it_ins = lru_access.insert(std::make_pair(key, lru_list.begin())).first;
			lru_list.push_front(std::make_pair(&it_ins->first, v));
			it_ins->second = lru_list.begin();
		}
	}

	void put_after(const K& key, const K& put_key, const V& v)
	{
		typename map_t::iterator it_after = lru_access.find(key);

		if (it_after != lru_access.end())
		{
			typename map_t::iterator it = lru_access.find(put_key);
			if (it != lru_access.end())
			{
				lru_list.splice(it_after->second, lru_list, it->second);
			}
			else
			{
				typename map_t::iterator it_ins = lru_access.insert(std::make_pair(put_key, lru_list.begin())).first;
				it_ins->second = lru_list.insert(it_after->second, std::make_pair(&it_ins->first, v));
			}
		}
		else
		{
			put(put_key, v);
		}
	}

	void put_back(const K& key, const V& v)
	{
		typename map_t::iterator it=lru_access.find(key);

		if(it!=lru_access.end())
		{
			it->second->second=v;
		}
		else
		{
			typename map_t::iterator it_ins = lru_access.insert(std::make_pair(key, lru_list.end())).first;
			lru_list.push_back(std::make_pair(&it_ins->first, v));
			it_ins->second = lru_list.end();
			--it_ins->second;
		}
	}

	bool change_key(const K& old_key, const K& new_key)
	{
		typename map_t::iterator it = lru_access.find(old_key);

		if (it != lru_access.end())
		{
			typename list_t::iterator it_list = it->second;
			lru_access.erase(it);
			typename map_t::iterator it_ins = lru_access.insert(std::make_pair(new_key, it_list)).first;
			it_list->first = &it_ins->first;
			return true;
		}
		return false;
	}

	bool check()
	{
		for (typename list_t::iterator it_list = lru_list.begin();
			it_list != lru_list.end(); ++it_list)
		{
			typename map_t::iterator it_map = lru_access.find(*it_list->first);
			if (it_map == lru_access.end())
				return false;

			if (it_map->second != it_list)
				return false;
		}

		for (typename map_t::iterator it_map = lru_access.begin();
			it_map != lru_access.end(); ++it_map)
		{
			if (*it_map->second->first != it_map->first)
				return false;
		}

		return true;
	}

	size_t size() const
	{
		return lru_access.size();
	}

	bool empty() const
	{
		return lru_access.empty();
	}

	V* get(const K& key, bool bring_front=true)
	{
		typename map_t::iterator it=lru_access.find(key);

		if(it!=lru_access.end())
		{
			if (bring_front)
			{
				bring_to_front(it);
			}

			if (it->first != key)
				abort();

			return &((*(it->second)).second);
		}
		else
		{
			return reinterpret_cast<V*>(0);
		}
	}

	bool has_key(const K& key)
	{
		typename map_t::iterator it=lru_access.find(key);

		return it!=lru_access.end();
	}

	std::pair<K, V> evict_one()
	{
		if(!lru_list.empty())
		{
			typename map_t::iterator it=lru_access.find(*lru_list.back().first);

			std::pair<K, V> ret(it->first, lru_list.back().second);

			lru_access.erase(it);
			lru_list.erase(--lru_list.end());

			return ret;
		}
		else
		{
			return std::pair<K, V>();
		}
	}

	typename list_t::iterator eviction_iterator_start()
	{
		return lru_list.end();
	}

	typename list_t::iterator eviction_iterator_finish()
	{
		return lru_list.begin();
	}

	std::pair<K, V> eviction_candidate(size_t skip=0)
	{
		if(!lru_list.empty())
		{
			typename 
			list_t::iterator it = lru_list.end();
			--it;
			
			for(size_t i=0;i<skip;++i)
			{
				if(it==lru_list.begin())
				{
					return std::pair<K, V>();
				}
				
				--it;
			}
		
			return std::make_pair(*it->first, it->second);
		}
		else
		{
			return std::pair<K, V>();
		}
	}

	void del(const K& key)
	{
		typename map_t::iterator it=lru_access.find(key);

		if(it!=lru_access.end())
		{
			lru_list.erase(it->second);
			lru_access.erase(it);
		}
	}

	list_t& get_list()
	{
		return lru_list;
	}

	void clear()
	{
		lru_list.clear();
		lru_access.clear();
	}

	void bring_to_front(typename list_t::iterator it)
	{
		lru_list.splice(lru_list.begin(), lru_list, it);
	}

private:

	void bring_to_front(typename map_t::iterator it)
	{
		lru_list.splice(lru_list.begin(), lru_list, it->second);
	}

	list_t lru_list;
	map_t lru_access;
};

}