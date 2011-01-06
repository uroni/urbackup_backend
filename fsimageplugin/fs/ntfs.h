#include "../../Interface/Types.h"
#include "../filesystem.h"

#pragma pack(push)
#pragma pack(1)

struct MFTAttribute
{
	unsigned int type;
	unsigned int length;
	unsigned char nonresident;
	unsigned char name_lenght;
	unsigned short name_offset;
	unsigned short flags;
	unsigned short attribute_id;
	unsigned int attribute_length;
	unsigned short attribute_offset;
	unsigned char indexed_flag;
	unsigned char padding1;
	//char padding2[488];
};

struct MFTAttributeNonResident
{
	unsigned int type;
	unsigned int lenght;
	unsigned char nonresident;
	unsigned char name_length;
	unsigned short name_offset;
	unsigned short flags;
	unsigned short attribute_id;
	uint64 starting_vnc;
	uint64 last_vnc;
	unsigned short run_offset;
	unsigned short compression_size;
	unsigned int padding;
	uint64 allocated_size;
	uint64 real_size;
	uint64 initial_size;
};

struct MFTAttributeFilename
{
	uint64 parent_ref;
	uint64 ctime;
	uint64 atime;
	uint64 mtime;
	uint64 rtime;
	uint64 allocated_size;
	uint64 real_size;
	unsigned int flags;
	unsigned int reparse;
	unsigned char filename_length;
	unsigned char filename_namespace;
};

struct NTFSBootRecord
{
	char jump[3];
	char magic[8];
	unsigned short bytespersector;
	unsigned char sectorspercluster;
	char unused1[7];
	unsigned char media_descriptor;
	char unused2[2];
	unsigned short sectorspertrack;
	unsigned short numberofheads;
	char unused3[8];
	char unknown[4];
	uint64 numberofsectors;
	uint64 mftlcn;
	uint64 mftmirrlcn;
	char clusterspermftrecord;
	char unused4[3];
	char clustersperindexrecord;
	char unused5[3];
	char volumeserialnumber[8];
	//char padding[432];
};

struct NTFSFileRecord
{
	char magic[4];
	unsigned short sequence_offset;
	unsigned short sequence_size;
	uint64 lsn;
	unsigned short squence_number;
	unsigned short hardlink_count;
	unsigned short attribute_offset;
	unsigned short flags;
	unsigned int real_size;
	unsigned int allocated_size;
	uint64 base_record;
	unsigned short next_id;
	//char padding[470];
};

#pragma pack(pop)

struct RunlistItem
{
	uint64 length;
	int64 offset;
};

class Runlist
{
public:
	Runlist(char *pData);

	void reset(void);
	bool getNext(RunlistItem &item);
	uint64 getSizeInClusters(void);
	uint64 getLCN(uint64 vcn);

private:
	char *data;
	char *pos;
};

class FSNTFS : public Filesystem
{
public:
	FSNTFS(const std::wstring &pDev);
	~FSNTFS(void);

	int64 getBlocksize(void);
	virtual int64 getSize(void);
	const unsigned char * getBitmap(void);

private:
	unsigned char *bitmap;

	_u32 sectorRead(int64 pos, char *buffer, _u32 bsize);
	bool applyFixups(char *data, size_t datasize, char* fixups, size_t fixups_size);

	bool has_error;

	unsigned int sectorsize;
	unsigned int clustersize;
	uint64 drivesize;
};