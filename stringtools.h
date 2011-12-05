#ifndef STRINGTOOLS_H
#define STRINGTOOLS_H

#include <string>
#include <vector>
#include <map>

std::string getafter(const std::string &str,const std::string &data);
std::string getafterinc(const std::string &str,const std::string &data);
std::wstring getafter(const std::wstring &str,const std::wstring &data);
std::wstring getafterinc(const std::wstring &str,const std::wstring &data);
std::string getbetween(std::string s1,std::string s2,std::string data);
std::string strdelete(std::string str,std::string data);
void writestring(std::string str,std::string file);
void writestring(char *str, unsigned int len,std::string file);
std::string getuntil(std::string str,std::string data);
std::wstring getuntil(std::wstring str,std::wstring data);
std::string getuntilinc(std::string str,std::string data);
std::string getline(int line,const std::string &str);
int linecount(const std::string &str);
std::string getFile(std::string filename);
std::wstring getFileUTF8(std::string filename);
std::string ExtractFileName(std::string fulln);
std::wstring ExtractFileName(std::wstring fulln);
std::string ExtractFilePath(std::string fulln);
std::wstring ExtractFilePath(std::wstring fulln);
std::wstring convert(bool pBool);
std::wstring convert(int i);
std::wstring convert(float f);
std::wstring convert(double f);
std::wstring convert(long long int i);
std::wstring convert(size_t i);
#if !defined(_WIN64)
std::wstring convert(unsigned long long int i);
#endif
std::wstring convert(unsigned int i);
std::string nconvert(bool pBool);
std::string nconvert(int i);
std::string nconvert(long long int i);
std::string nconvert(size_t i);
#if !defined(_WIN64)
std::string nconvert(unsigned long long int i);
#endif
std::string nconvert(unsigned int i);
std::string nconvert(float f);
std::string nconvert(double f);
std::string findextension(const std::string& pString);
std::string wnarrow(const std::wstring& pStr);
std::wstring widen(std::string tw);
std::string replaceonce(std::string tor, std::string tin, std::string data);
std::wstring replaceonce(std::wstring tor, std::wstring tin, std::wstring data);
void Tokenize(const std::string& str, std::vector<std::string> &tokens, std::string seps);
void Tokenize(const std::wstring& str, std::vector<std::wstring> &tokens, std::wstring seps);
void TokenizeMail(const std::string& str, std::vector<std::string> &tokens, std::string seps);
void TokenizeMail(const std::wstring& str, std::vector<std::wstring> &tokens, std::wstring seps);
bool isnumber(char ch);
bool isletter(char ch);
bool isnumber(wchar_t ch);
bool isletter(wchar_t ch);
void strupper(std::string *pStr);
void strupper(std::wstring *pStr);
std::string greplace(std::string tor, std::string tin, std::string data);
std::wstring greplace(std::wstring tor, std::wstring tin, std::wstring data);
int getNextNumber(const std::string &pStr, int *read=NULL);
std::string strlower(const std::string &str);
bool next(const std::string &pData, const size_t & doff, const std::string &pStr);
bool next(const std::wstring &pData, const size_t & doff, const std::wstring &pStr);
char getRandomChar(void);
std::string getRandomNumber(void);
void transformHTML(std::string &str);
void EscapeSQLString(std::string &pStr);
void EscapeSQLString(std::wstring &pStr);
void EscapeCh(std::string &pStr, char ch='\\');
void EscapeCh(std::wstring &pStr, wchar_t ch);
std::string UnescapeSQLString(std::string pStr);
std::wstring UnescapeSQLString(std::wstring pStr);
void ParseParamStr(const std::string &pStr, std::map<std::wstring,std::wstring> *pMap);
int round(float f);
std::string FormatTime(int timeins);
bool IsHex(const std::string &str);
unsigned long hexToULong(const std::string &data);
std::string byteToHex(unsigned char ch);
std::string bytesToHex(const unsigned char *b, size_t bsize);
std::wstring htmldecode(std::string str, bool html=true, char xc='%');
bool checkhtml(const std::string &str);
std::string nl2br(std::string str);
bool FileExists(std::string pFile);
bool checkStringHTML(const std::string &str);
std::string ReplaceChar(std::string str, char tr, char ch);
std::wstring ReplaceChar(std::wstring str, wchar_t tr, wchar_t ch);
std::string striptags(std::string html);
std::string base64_encode(unsigned char const* , unsigned int len);
std::string base64_decode(std::string const& s);
bool CheckForIllegalChars(const std::string &str);
int watoi(std::wstring str);
std::wstring strlower(const std::wstring &str);
std::string trim(const std::string &str);
std::wstring trim(const std::wstring &str);
void replaceNonAlphaNumeric(std::string &str, char rch);
std::string conv_filename(std::string fn);
std::string UnescapeHTML(const std::string &html);
std::wstring UnescapeHTML(const std::wstring &html);
#endif
