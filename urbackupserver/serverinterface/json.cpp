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

#ifndef CLIENT_ONLY

#include "json.h"
#include "../../stringtools.h"
#include "../../Interface/Server.h"

namespace JSON
{
	//---------------Array-------------------
	Array::Array(void)
	{
	}
	
	Array::Array(const std::vector<Value> &vals)
	{
		data=vals;
	}

	void Array::add(const Value &val)
	{
		data.push_back(val);
	}

	void Array::clear(void)
	{
		data.clear();
	}
	
	size_t Array::size(void)
	{
		return data.size();
	}
	
	void Array::erase(size_t idx)
	{
		data.erase(data.begin()+idx);
	}

	std::string Array::get(bool compressed)
	{
		std::string r="[";
		for(size_t i=0;i<data.size();++i)
		{
			r+=data[i].get(compressed);
			if(i+1<data.size())
				r+=",";
		}
		r+="]";
		return r;
	}

	//---------------Object-------------------
	Object::Object(void)
	{
	}

	Object::Object(const std::map<std::string, Value> &objdata)
	{
		data=objdata;
	}

	void Object::set(const std::string &key, const Value &val)
	{
		data[key]=val;
	}

	void Object::erase(const std::string &key)
	{
		std::map<std::string, Value>::iterator it=data.find(key);
		if(it!=data.end())
			data.erase(it);
	}

	Value Object::get(const std::string &key)
	{
		std::map<std::string, Value>::iterator it=data.find(key);
		if(it!=data.end())
			return it->second;
		else
			return Value();
	}

	std::string Object::get(bool compressed)
	{
		std::string r="{";
		if(!compressed)
			r+="\n";
		std::map<std::string, Value>::iterator last=data.end();
		if(!data.empty())
			--last;
		for(std::map<std::string, Value>::iterator it=data.begin();it!=data.end();++it)
		{
			r+="\""+it->first+"\": "+it->second.get(compressed);
			if(it!=last)
			{
				r+=",";
			}
			if(!compressed)
				r+="\n";
		}
		r+="}";
		if(!compressed)
			r+="\n";
		return r;
	}

	//---------------Value-------------------
	Value::Value(void)
	{
		init();
	}

	Value::Value(const std::string &val)
	{
		init(val);
	}

	Value::Value(const std::wstring &val)
	{
		init(val);
	}

	Value::Value(const Object &val)
	{
		init(val);
	}

	Value::Value(const Array &val)
	{
		init(val);
	}

	Value::Value(bool val)
	{
		init(val);
	}

	Value::Value(int val)
	{
		init(val);
	}

	Value::Value(unsigned int val)
	{
		init(val);
	}

	Value::Value(_i64 val)
	{
		init(val);
	}

	Value::Value(uint64 val)
	{
		init(val);
	}

	Value::Value(double val)
	{
		init(val);
	}
	
	Value::Value(const char *val)
	{
		init(val);
	}
	
	Value::Value(const wchar_t *val)
	{
		init(val);
	}

	void Value::init(void)
	{
		data_type=null_type;
		data=NULL;
	}
	
	void Value::init(const char *val)
	{
		data_type=str_type;
		data=new std::string(val);
	}
	
	void Value::init(const wchar_t *val)
	{
		data_type=wstr_type;
		data=new std::wstring(val);
	}

	void Value::init(const std::string &val)
	{
		data_type=str_type;
		data=new std::string(val);
	}

	void Value::init(const std::wstring &val)
	{
		data_type=wstr_type;
		data=new std::wstring(val);
	}

	void Value::init(const Object &val)
	{
		data_type=obj_type;
		data=new Object(val);
	}

	void Value::init(const Array &val)
	{
		data_type=array_type;
		data=new Array(val);
	}

	void Value::init(bool val)
	{
		data_type=bool_type;
		data=new bool;
		*((bool*)data)=val;
	}

	void Value::init(int val)
	{
		data_type=int_type;
		data=new int;
		*((int*)data)=val;
	}

	void Value::init(unsigned int val)
	{
		data_type=uint_type;
		data=new unsigned int;
		*((unsigned int*)data)=val;
	}

	void Value::init(_i64 val)
	{
		data_type=int64_type;
		data=new _i64;
		*((_i64*)data)=val;
	}

	void Value::init(uint64 val)
	{
		data_type=uint64_type;
		data=new uint64;
		*((uint64*)data)=val;
	}

	void Value::init(double val)
	{
		data_type=double_type;
		data=new double;
		*((double*)data)=val;
	}

	Value::~Value()
	{
		switch(data_type)
		{
			case str_type: delete ((std::string*)data); break;
			case wstr_type: delete ((std::wstring*)data); break;
			case obj_type: delete ((Object*)data); break;
			case array_type: delete ((Array*)data); break;
			case bool_type: delete ((bool*)data); break;
			case int_type: delete ((int*)data); break;
			case uint_type: delete ((unsigned int*)data); break;
			case int64_type: delete ((_i64*)data); break;
			case uint64_type: delete ((uint64*)data); break;
			case double_type: delete ((double*)data); break;
		}
	}

	void Value::init(const Value &other)
	{
		Value_type other_type=other.getType();
		data_type=other_type;
		switch(other_type)
		{
			case str_type: init(other.getString()); break;
			case wstr_type: init(other.getWString()); break;
			case obj_type: init(other.getObject()); break;
			case array_type: init(other.getArray()); break;
			case bool_type: init(other.getBool()); break;
			case int_type: init(other.getInt()); break;
			case uint_type: init(other.getUInt()); break;
			case int64_type: init(other.getInt64()); break;
			case uint64_type: init(other.getUInt64()); break;
			case double_type: init(other.getDouble()); break;
			default: data=NULL; break;
		}
	}
	Value::Value(const Value &other)
	{
		init(other);
	}

	Value &  Value::operator=(const Value &other)
	{
		init(other);
		return *this;
	}

	std::string Value::escape(const std::string &t)
	{
		std::string r;
		for(size_t i=0;i<t.size();++i)
		{
			if(t[i]=='\\')
			{
				r+="\\\\";
			}
			else if(t[i]=='"')
			{
				r+="\\\"";
			}
			else if(t[i]=='\n')
			{
				r+="\\n";
			}
			else if(t[i]=='\r')
			{
				r+="\\r";
			}
			else
			{
				r+=t[i];
			}
		}
		return r;
	}

	std::string Value::get(bool compressed)
	{
		switch(data_type)
		{
			case str_type: return "\""+escape(*((std::string*)data))+"\"";
			case wstr_type: return "\""+escape(Server->ConvertToUTF8(*((std::wstring*)data)))+"\"";
			case obj_type: return ((Object*)data)->get(compressed);
			case array_type: return ((Array*)data)->get(compressed);
			case bool_type: return nconvert(*((bool*)data));
			case int_type: return nconvert(*((int*)data));
			case uint_type: return nconvert(*((unsigned int*)data));
			case int64_type: return nconvert(*((_i64*)data));
			case uint64_type: return nconvert(*((uint64*)data));
			case double_type: return nconvert(*((double*)data));
			default: return "null";
		}
	}

	const std::string & Value::getString(void) const
	{
		if(data_type==str_type)
		{
			return *((std::string*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	const std::wstring & Value::getWString(void) const
	{
		if(data_type==wstr_type)
		{
			return *((std::wstring*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	const Object & Value::getObject(void) const
	{
		if(data_type==obj_type)
		{
			return *((Object*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	const Array & Value::getArray(void) const
	{
		if(data_type==array_type)
		{
			return *((Array*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	bool Value::getBool(void) const
	{
		if(data_type==bool_type)
		{
			return *((bool*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	int Value::getInt(void) const
	{
		if(data_type==int_type)
		{
			return *((int*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	unsigned int Value::getUInt(void) const
	{
		if(data_type==uint_type)
		{
			return *((unsigned int*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	_i64 Value::getInt64(void) const
	{
		if(data_type==int64_type)
		{
			return *((_i64*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	uint64 Value::getUInt64(void) const
	{
		if(data_type==uint64_type)
		{
			return *((uint64*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	double Value::getDouble(void) const
	{
		if(data_type==double_type)
		{
			return *((double*)data);
		}
		else
		{
			throw BadTypeException();
		}
	}

	Value_type Value::getType(void) const
	{
		return data_type;
	}
}

#endif //CLIENT_ONLY