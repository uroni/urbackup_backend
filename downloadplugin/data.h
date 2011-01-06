#include <string>
#include <vector>

class CWData
{
public:
	char* getDataPtr(void);
	unsigned long getDataSize(void);

	void addInt(int ta);
	void addUInt(unsigned int ta);
	void addFloat(float ta);
	void addString(std::string ta);
	void addChar(char ta);
	void addUChar(unsigned char ta);

private:
	std::vector<char> data;
};

class CRData
{
public:
	CRData(const char* c,size_t datalength);

	bool getInt(int *ret);
	bool getUInt(unsigned int *ret);
	bool getFloat(float *ret);
	bool getStr(std::string *ret);
	bool getChar(char *ret);
	bool getUChar(unsigned char *ret);

	unsigned int getSize(void);
	unsigned int getLeft(void);
	unsigned int getStreampos(void);

private:

	char* data;
	size_t streampos;
	size_t datalen;	
};

