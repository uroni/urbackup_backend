#ifndef TYPES_H
#define TYPES_H


#include <string>
#include <map>
#include <vector>

typedef int THREAD_ID;
typedef int DATABASE_ID;
typedef int PLUGIN_ID;
typedef unsigned int THREADPOOL_TICKET;
typedef int POSTFILE_KEY;

#ifdef LINUX
typedef long long int __int64;
typedef unsigned long long int uint64;
#elif _WIN32
typedef unsigned __int64 uint64;
#else
typedef long long int __int64;
typedef unsigned long long int uint64;
#endif

#ifndef NULL
#define NULL    0
#endif

typedef __int64 int64;
typedef unsigned char uchar;
typedef int _i32;
typedef __int64 _i64;
typedef unsigned int _u32;
typedef unsigned short _u16;
typedef short _i16;

const THREAD_ID ILLEGAL_THREAD_ID=-1;
const PLUGIN_ID ILLEGAL_PLUGIN_ID=-1;
const THREADPOOL_TICKET ILLEGAL_THREADPOOL_TICKET=0;

typedef std::map<std::wstring,std::wstring> str_map;
typedef std::map<std::wstring,float> float_map;
typedef std::map<std::wstring,int> int_map;

typedef std::map<std::string,std::string> str_nmap;
typedef std::map<std::string,float> float_nmap;
typedef std::map<std::string,int> int_nmap;

typedef std::map<std::string, std::string> db_nsingle_result;
typedef std::vector< db_nsingle_result > db_nresults;


typedef std::map<std::wstring, std::wstring> db_single_result;
typedef std::vector< db_single_result > db_results;

#ifdef _WIN32
#if  _MSC_VER<1600
const class
{
public:
  template<class T> operator T*() const {return 0;}
  template<class C, class T> operator T C::*() const {return 0;}
private:
  void operator&() const;
}
nullptr = {};
#endif
#else
const class
{
public:
  template<class T> operator T*() const {return 0;}
  template<class C, class T> operator T C::*() const {return 0;}
private:
  void operator&() const;
}
nullptr = {};
#endif


#endif
