#include <vector>
#include <string>
#include <map>
#include <exception>

#include "../../Interface/Types.h"

namespace JSON
{
	class Value;

	class Array
	{
	public:
		Array(void);
		Array(const std::vector<Value> &vals);

		void add(const Value &val);
		void clear(void);
		size_t size(void);
		void erase(size_t idx);

		std::string get(bool compressed);
	private:
		std::vector<Value> data;
	};

	class Object
	{
	public:
		Object(void);
		Object(const std::map<std::string, Value> &objdata);

		void set(const std::string &key, const Value &val);
		void erase(const std::string &key);
		Value get(const std::string &key);

		std::string get(bool compressed);

	private:
		std::map<std::string, Value> data;
	};

	enum Value_type
	{
		str_type,
		wstr_type,
		obj_type,
		array_type, 
		bool_type, 
		int_type,
		uint_type,
		int64_type,
		uint64_type,
		double_type,
		luint_type,
		null_type
	};

	class BadTypeException: public std::exception
	{
	  virtual const char* what() const throw()
	  {
		return "Bad value type";
	  }
	};

	class Value
	{
	public:
		Value(void);
		Value(const Value &other);
		Value(const std::string &val);
		Value(const std::wstring &val);
		Value(const Object &val); 
		Value(const Array &val);
		Value(bool val);
		Value(int val);
		Value(unsigned int val);
		Value(_i64 val);
		Value(uint64 val);
		Value(double val);
		Value(const char* val);
		Value(const wchar_t* val);
		Value(long unsigned int val);
		~Value();

		Value & operator=(const Value &other);

		std::string get(bool compressed);

		const std::string & getString(void) const;
		const std::wstring & getWString(void) const;
		const Object & getObject(void) const;
		const Array & getArray(void) const;
		bool getBool(void) const;
		int getInt(void) const;
		unsigned int getUInt(void) const;
		_i64 getInt64(void) const;
		uint64 getUInt64(void) const;
		double getDouble(void) const;
		long unsigned int getLUInt(void) const;

		Value_type getType(void) const;

	private:
		void init(void);
		void init(const Value &other);
		void init(const std::string &val);
		void init(const std::wstring &val);
		void init(const Object &val); 
		void init(const Array &val);
		void init(bool val);
		void init(int val);
		void init(unsigned int val);
		void init(_i64 val);
		void init(uint64 val);
		void init(double val);
		void init(const char* val);
		void init(const wchar_t* val);
		void init(long unsigned int val);

		std::wstring escape(const std::wstring &t);

		void *data;
		Value_type data_type;
	};
}