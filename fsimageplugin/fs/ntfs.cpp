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

#include "../../Interface/Server.h"
#include "../../stringtools.h"
#include "ntfs.h"
#include "../../urbackupcommon/os_functions.h"
#include <math.h>
#include <set>
#include <memory.h>

#ifndef _WIN32
#define UD_UINT64 0xFFFFFFFFFFFFFFFFULL
#else
#define UD_UINT64 0xFFFFFFFFFFFFFFFF
#endif

class MemFree
{
public:
	MemFree(char *buf) : buf(buf) {}
	~MemFree(void) { delete []buf; }

private:
	char* buf;
};

FSNTFS::FSNTFS(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback, bool check_mft_mirror, bool fix)
	: Filesystem(pDev, read_ahead, next_block_callback), bitmap(NULL)
{
	init(check_mft_mirror, fix);
	initReadahead(read_ahead, background_priority);
}

FSNTFS::FSNTFS(IFile *pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback, bool check_mft_mirror, bool fix)
	: Filesystem(pDev, next_block_callback), bitmap(NULL)
{
	init(check_mft_mirror, fix);
	initReadahead(read_ahead, background_priority);
}

void FSNTFS::init(bool check_mft_mirror, bool fix)
{
	if(has_error)
		return;

	bitmap=NULL;
	sectorsize=512;
	NTFSBootRecord br;
	_u32 rc=sectorRead(0, (char*)&br, sizeof(NTFSBootRecord) );
	if(rc!=sizeof(NTFSBootRecord) )
	{
		has_error=true;
		Server->Log("Error reading boot record", LL_ERROR);
		return;
	}
	if( br.magic[0]!='N' || br.magic[1]!='T' || br.magic[2]!='F' || br.magic[3]!='S' )
	{
		has_error=true;
		Server->Log("NTFS magic wrong", LL_ERROR);
		return;
	}

	sectorsize=br.bytespersector;
	clustersize=sectorsize*br.sectorspercluster;
	drivesize=br.numberofsectors*sectorsize;

	Server->Log("Sectorsize: "+convert(sectorsize), LL_DEBUG);
	Server->Log("Clustersize: "+convert(clustersize), LL_DEBUG);
	Server->Log("ClustersPerMFTNode Offset: "+convert((uint64)&br.mftlcn-(uint64)&br), LL_DEBUG);

	unsigned int mftrecordsize;
	if(br.clusterspermftrecord<0)
	{
		mftrecordsize=1 << (-br.clusterspermftrecord);
	}
	else
	{
		mftrecordsize=br.clusterspermftrecord*clustersize;
	}

	uint64 mftstart=br.mftlcn*clustersize;
	Server->Log("MFTStart: "+convert(br.mftlcn), LL_DEBUG);

	char *mftrecord=new char[mftrecordsize];
	MemFree mftrecord_free(mftrecord);

	rc=sectorRead(mftstart, mftrecord, mftrecordsize);
	if(rc!=mftrecordsize )
	{
		has_error=true;
		Server->Log("Error reading MFTRecord", LL_ERROR);
		return;
	}

	NTFSFileRecord mft;
	memcpy((char*)&mft, mftrecord, sizeof(NTFSFileRecord) );

	if(!applyFixups(mftrecord, mftrecordsize, mftrecord+mft.sequence_offset, mft.sequence_size*2 ) )
	{
		Server->Log("Applying fixups failed", LL_ERROR);
		has_error=true;
		return;
	}
	
	if(mft.magic[0]!='F' || mft.magic[1]!='I' || mft.magic[2]!='L' || mft.magic[3]!='E' )
	{
		has_error=true;
		Server->Log("NTFSFileRecord magic wrong", LL_ERROR);
		return;
	}

	_u32 currpos=0;
	MFTAttribute attr;
	attr.length=mft.attribute_offset;
	do
	{
		currpos+=attr.length;
		memcpy((char*)&attr, mftrecord+currpos, sizeof(MFTAttribute) );
		if(attr.type==0x30 && attr.nonresident==0) //FILENAME
		{
			MFTAttributeFilename fn;
			memcpy((char*)&fn, mftrecord+currpos+attr.attribute_offset, sizeof(MFTAttributeFilename) );
			std::string fn_uc;
			fn_uc.resize(fn.filename_length*2);
			memcpy(&fn_uc[0], mftrecord+currpos+attr.attribute_offset+sizeof(MFTAttributeFilename), fn.filename_length*2);
			Server->Log("Filename="+Server->ConvertFromUTF16(fn_uc) , LL_DEBUG);
		}
		Server->Log("Attribute Type: "+convert(attr.type)+" nonresident="+convert(attr.nonresident)+" length="+convert(attr.length), LL_DEBUG);
	}while( attr.type!=0xFFFFFFFF && attr.type!=0x80);

	if(attr.type==0xFFFFFFFF )
	{
		has_error=true;
		Server->Log("Data attribute not found", LL_ERROR);
		return;
	}

	if(attr.nonresident!=1)
	{
		Server->Log("DATA is resident!! - unexpected", LL_ERROR);
		has_error=true;
		return;
	}

	MFTAttributeNonResident datastream;
	memcpy((char*)&datastream, mftrecord+currpos, sizeof(MFTAttributeNonResident) );
	if(datastream.compression_size!=0)
	{
		Server->Log("MFT Rundata is compressed. Can't handle that", LL_ERROR);
		has_error=true;
		return;
	}

	Runlist mftrunlist(mftrecord+currpos+datastream.run_offset );

	this->mftrecordsize=mftrecordsize;
	mftsize=datastream.real_size;
	mftrunlist_data.assign(mftrecord+currpos+datastream.run_offset, mftrecord+currpos+attr.length);

	unsigned int bitmap_vcn=(6*mftrecordsize)/clustersize;
	uint64 bitmap_lcn=mftrunlist.getLCN(bitmap_vcn);
	if(bitmap_lcn==UD_UINT64)
	{
		Server->Log("Error mapping VCN to LCN", LL_ERROR);
		has_error=true;
		return;
	}
	uint64 bitmap_pos=bitmap_lcn*clustersize+(6*mftrecordsize)%clustersize;
	char *bitmaprecord=new char[mftrecordsize];
	MemFree bitmaprecord_free(bitmaprecord);

	rc=sectorRead(bitmap_pos, bitmaprecord, mftrecordsize);
	if(rc==0)
	{
		Server->Log("Error reading bitmap MFT entry", LL_DEBUG);
		has_error=true;
		return;
	}

	NTFSFileRecord bitmapf;
	memcpy((char*)&bitmapf, bitmaprecord, sizeof(NTFSFileRecord) );

	if(!applyFixups(bitmaprecord, mftrecordsize, bitmaprecord+bitmapf.sequence_offset, bitmapf.sequence_size*2 ) )
	{
		Server->Log("Applying fixups failed", LL_ERROR);
		has_error=true;
		return;
	}
	
	if(bitmapf.magic[0]!='F' || bitmapf.magic[1]!='I' || bitmapf.magic[2]!='L' || bitmapf.magic[3]!='E' )
	{
		has_error=true;
		Server->Log("NTFSFileRecord magic wrong -2", LL_ERROR);
		return;
	}

	currpos=0;
	attr.length=mft.attribute_offset;
	bool is_bitmap=false;
	do
	{
		currpos+=attr.length;
		memcpy((char*)&attr, bitmaprecord+currpos, sizeof(MFTAttribute) );
		if(attr.type==0x30 && attr.nonresident==0) //FILENAME
		{
			MFTAttributeFilename fn;
			memcpy((char*)&fn, bitmaprecord+currpos+attr.attribute_offset, sizeof(MFTAttributeFilename) );
			std::string fn_uc;
			fn_uc.resize(fn.filename_length*2);
			memcpy(&fn_uc[0], bitmaprecord+currpos+attr.attribute_offset+sizeof(MFTAttributeFilename), fn.filename_length*2);
			Server->Log("Filename="+Server->ConvertFromUTF16(fn_uc) , LL_DEBUG);
			if(Server->ConvertFromUTF16(fn_uc)=="$Bitmap")
			{
				is_bitmap=true;
			}
		}
		Server->Log("Attribute Type: "+convert(attr.type)+" nonresident="+convert(attr.nonresident)+" length="+convert(attr.length), LL_DEBUG);
	}while( attr.type!=0xFFFFFFFF && attr.type!=0x80);

	if(!is_bitmap)
	{
		Server->Log("Filename attribute not found or filename wrong", LL_ERROR);
		has_error=true;
		return;
	}

	if(attr.type!=0x80)
	{
		Server->Log("Data Attribute of Bitmap not found", LL_ERROR);
		has_error=true;
		return;
	}

	if(attr.nonresident!=1)
	{
		Server->Log("DATA is resident!! - unexpected -2", LL_ERROR);
		has_error=true;
		return;
	}

	MFTAttributeNonResident bitmapstream;
	memcpy((char*)&bitmapstream, bitmaprecord+currpos, sizeof(MFTAttributeNonResident) );
	if(bitmapstream.compression_size!=0)
	{
		Server->Log("MFT Rundata is compressed. Can't handle that. -2", LL_ERROR);
		has_error=true;
		return;
	}

	Runlist bitmaprunlist(bitmaprecord+currpos+bitmapstream.run_offset );
	
	Server->Log("Bitmap size="+convert(bitmapstream.real_size), LL_DEBUG);
	bitmap=new unsigned char[(unsigned int)bitmapstream.real_size];
	char *buffer=new char[clustersize];
	MemFree buffer_free(buffer);
	bitmap_pos=0;
	for(uint64 i=bitmapstream.starting_vnc;i<=bitmapstream.last_vnc;++i)
	{
		uint64 lcn=bitmaprunlist.getLCN(i);
		if(lcn==UD_UINT64)
		{
			Server->Log("Error mapping VCN->LCN. -2", LL_ERROR);
			has_error=true;
			return;
		}
		dev->Seek(lcn*clustersize);
		rc=dev->Read(buffer, clustersize);
		if(rc!=clustersize)
		{
			Server->Log("Error reading cluster "+convert(lcn)+" code: 529", LL_ERROR);
			has_error=true;
			return;
		}
		memcpy(&bitmap[bitmap_pos], buffer, (size_t)(std::min)(bitmapstream.real_size-bitmap_pos, (uint64)clustersize) );
		bitmap_pos+=(std::min)(bitmapstream.real_size-bitmap_pos, (uint64)clustersize);
	}

	if(check_mft_mirror)
	{
		if(!checkMFTMirror(mftrecordsize, mftrunlist, mft, true) )
		{
			Server->Log("MFT mirror check failed", LL_ERROR);
			has_error=true;
			return;
		}
	}
}

FSNTFS::~FSNTFS(void)
{
	delete [] bitmap;
}

_u32 FSNTFS::sectorRead(int64 pos, char *buffer, _u32 bsize)
{
	int64 rpos=pos-pos%sectorsize;
	dev->Seek(rpos);
	_u32 rbsize=(_u32)(pos-rpos)+bsize;
	rbsize=rbsize+(sectorsize-rbsize%sectorsize);
	char *rbuf=new char[rbsize];
	_u32 read=dev->Read(rbuf, rbsize);
	if(read!=rbsize && read<(pos-rpos)+bsize)
	{
		return 0;
	}
	memcpy(buffer, &rbuf[pos-rpos], bsize);
	delete [] rbuf;
	return bsize;
}

bool FSNTFS::applyFixups(char *data, size_t datasize, char* fixups, size_t fixups_size)
{
	unsigned int num_fixups=(unsigned int)datasize/sectorsize;
	if(num_fixups>(fixups_size-2)/2)
	{
		Server->Log("Number of fixups wrong!", LL_ERROR);
		return false;
	}
	char seq_number[2];
	memcpy(seq_number, fixups, 2);
	
	size_t t=0;
	for(size_t i=2;i<fixups_size;i+=2,++t)
	{
		if( data[(t+1)*sectorsize-2]!=seq_number[0] || data[(t+1)*sectorsize-1]!=seq_number[1] )
		{
			Server->Log("Cluster corrupted. Stopping. (Testing fixup failed)", LL_ERROR);
			return false;
		}
		data[(t+1)*sectorsize-2]=fixups[i];
		data[(t+1)*sectorsize-1]=fixups[i+1];
	}

	return true;
}

int64 FSNTFS::getBlocksize(void)
{
	return clustersize;
}

int64 FSNTFS::getSize(void)
{
	return drivesize;
}

const unsigned char * FSNTFS::getBitmap(void)
{
	return bitmap;
}

void FSNTFS::logFileChanges(std::string volpath, int64 min_size, char * fc_bitmap)
{
}

std::string FSNTFS::getType()
{
	return "ntfs";
}

namespace
{
	const unsigned int mft_attr_attribute_list=0x20;
	const unsigned int mft_attr_filename=0x30;
	const unsigned int mft_attr_data=0x80;
	const unsigned int mft_attr_end=0xFFFFFFFF;
	const unsigned short mft_record_in_use=1;
	const unsigned short mft_record_directory=2;
	const unsigned char mft_filename_dos=2;
	const uint64 mft_ref_mask=0xFFFFFFFFFFFFULL;
	const uint64 mft_root_record=5;

	bool nextAttribute(const char* record, unsigned int recordsize, unsigned int& pos, MFTAttribute& attr)
	{
		if(pos+sizeof(MFTAttribute)>recordsize)
			return false;
		memcpy(&attr, record+pos, sizeof(MFTAttribute));
		return attr.type!=mft_attr_end && attr.length>=sizeof(MFTAttribute) && pos+attr.length<=recordsize;
	}

	struct SMftName
	{
		uint64 parent_ref;
		std::string name;
	};

	//Win32/POSIX names; the 8.3 aliases only when there is nothing else
	void fileNames(const char* record, unsigned int recordsize, std::vector<SMftName>& names)
	{
		std::vector<SMftName> dos_names;
		unsigned int pos=reinterpret_cast<const NTFSFileRecord*>(record)->attribute_offset;
		MFTAttribute attr;
		for(;nextAttribute(record, recordsize, pos, attr);pos+=attr.length)
		{
			if(attr.type!=mft_attr_filename || attr.nonresident!=0
				|| pos+attr.attribute_offset+sizeof(MFTAttributeFilename)>recordsize)
				continue;

			MFTAttributeFilename fn;
			memcpy(&fn, record+pos+attr.attribute_offset, sizeof(MFTAttributeFilename));
			size_t name_pos=pos+attr.attribute_offset+sizeof(MFTAttributeFilename);
			if(name_pos+fn.filename_length*2>recordsize)
				continue;

			SMftName name;
			name.parent_ref=fn.parent_ref;
			name.name=Server->ConvertFromUTF16(std::string(record+name_pos, fn.filename_length*2));
			(fn.filename_namespace==mft_filename_dos ? dos_names : names).push_back(name);
		}
		if(names.empty())
			names=dos_names;
	}

	struct SDirEntry
	{
		uint64 parent_ref;
		unsigned short sequence_number;
		std::string name;
	};
}

class FSNTFS::IMftRecordVisitor
{
public:
	virtual void record(uint64 recno, const char* data, const NTFSFileRecord& header) = 0;
};

bool FSNTFS::readMftClusters(uint64 vcn, uint64 count, char* buf)
{
	Runlist runlist(mftrunlist_data.data());
	for(uint64 i=0;i<count;)
	{
		uint64 lcn=runlist.getLCN(vcn+i);
		if(lcn==UD_UINT64)
			return false;
		uint64 n=1;
		while(i+n<count && runlist.getLCN(vcn+i+n)==lcn+n)
			++n;
		dev->Seek(lcn*clustersize);
		if(dev->Read(buf+i*clustersize, static_cast<_u32>(n*clustersize))!=n*clustersize)
			return false;
		i+=n;
	}
	return true;
}

bool FSNTFS::walkMft(IMftRecordVisitor& visitor)
{
	const uint64 chunk_clusters=(1024*1024)/clustersize;
	std::vector<char> buf(static_cast<size_t>(chunk_clusters*clustersize));
	uint64 nrecords=mftsize/mftrecordsize;
	for(uint64 vcn=0, recno=0;recno<nrecords;vcn+=chunk_clusters)
	{
		uint64 count=(std::min)(chunk_clusters, (mftsize-vcn*clustersize+clustersize-1)/clustersize);
		if(!readMftClusters(vcn, count, buf.data()))
		{
			Server->Log("Error reading MFT cluster "+convert(vcn), LL_ERROR);
			return false;
		}
		for(size_t off=0;off+mftrecordsize<=count*clustersize && recno<nrecords;off+=mftrecordsize, ++recno)
		{
			char* record=buf.data()+off;
			NTFSFileRecord header;
			memcpy(&header, record, sizeof(NTFSFileRecord));
			if(memcmp(header.magic, "FILE", 4)!=0
				|| !(header.flags & mft_record_in_use)
				|| header.sequence_offset+header.sequence_size*2>mftrecordsize
				|| !applyFixups(record, mftrecordsize, record+header.sequence_offset, header.sequence_size*2))
				continue;
			visitor.record(recno, record, header);
		}
	}
	return true;
}

int64 FSNTFS::excludeMatchingFiles(const std::string& volume_root, IFsExcludeCallback* callback)
{
	return excludeMatchingFilesInto(volume_root, callback, this);
}

int64 FSNTFS::excludeMatchingFilesInto(const std::string& volume_root, IFsExcludeCallback* callback, Filesystem* target)
{
	//Pass 1: the directory tree, so that every file name can be resolved to a full path
	class DirVisitor : public IMftRecordVisitor
	{
	public:
		std::map<uint64, SDirEntry> dirs;

		virtual void record(uint64 recno, const char* data, const NTFSFileRecord& header)
		{
			if(!(header.flags & mft_record_directory) || header.base_record!=0)
				return;
			std::vector<SMftName> names;
			fileNames(data, header.real_size, names);
			if(names.empty())
				return;
			SDirEntry& e=dirs[recno];
			e.parent_ref=names[0].parent_ref;
			e.sequence_number=header.squence_number;
			e.name=names[0].name;
		}
	} dir_visitor;

	if(!walkMft(dir_visitor))
		return -1;

	//Pass 2: match every file with all its names (hard links share the data) and drop the data runs of the matches
	class FileVisitor : public IMftRecordVisitor
	{
	public:
		FileVisitor(Filesystem* fs, const std::string& root, const std::map<uint64, SDirEntry>& dirs, IFsExcludeCallback* callback)
			: fs(fs), root(root), dirs(dirs), callback(callback), total_clusters(fs->getSize()/fs->getBlocksize()),
			  excluded_bytes(0), n_excluded(0) {}

		bool path(uint64 dir_ref, std::string& out, size_t depth=0)
		{
			uint64 recno=dir_ref & mft_ref_mask;
			if(recno==mft_root_record)
			{
				out=root;
				return true;
			}
			std::map<uint64, std::string>::iterator it_cached=paths.find(recno);
			if(it_cached!=paths.end())
			{
				out=it_cached->second;
				return true;
			}
			std::map<uint64, SDirEntry>::const_iterator it=dirs.find(recno);
			if(depth>255 || it==dirs.end() || it->second.sequence_number!=(dir_ref>>48))
				return false;
			if(!path(it->second.parent_ref, out, depth+1))
				return false;
			out+=os_file_sep()+it->second.name;
			paths[recno]=out;
			return true;
		}

		bool isExcluded(const char* data, const NTFSFileRecord& header)
		{
			std::vector<SMftName> names;
			fileNames(data, header.real_size, names);
			if(names.empty())
				return false;
			for(size_t i=0;i<names.size();++i)
			{
				std::string p;
				if(!path(names[i].parent_ref, p) || !callback->isExcluded(p+os_file_sep()+names[i].name))
					return false;
			}
			return true;
		}

		void excludeData(const char* data, const NTFSFileRecord& header)
		{
			unsigned int pos=header.attribute_offset;
			MFTAttribute attr;
			for(;nextAttribute(data, header.real_size, pos, attr);pos+=attr.length)
			{
				if(attr.type!=mft_attr_data || attr.nonresident!=1)
					continue;
				MFTAttributeNonResident nr;
				memcpy(&nr, data+pos, sizeof(MFTAttributeNonResident));
				const char* p=data+pos+nr.run_offset;
				const char* end=data+pos+attr.length;
				int64 lcn=0;
				while(p<end && *p!=0)
				{
					unsigned char length_size=*p & 0x0F;
					unsigned char offset_size=*p >> 4;
					if(p+1+length_size+offset_size>end || length_size>8 || offset_size>8)
						break;
					uint64 length=0;
					memcpy(&length, p+1, length_size);
					if(offset_size>0) //otherwise a sparse run without clusters
					{
						int64 offset=0;
						memcpy(&offset, p+1+length_size, offset_size);
						if(offset_size<8 && (offset>>(offset_size*8-1))&1)
							offset-=(int64)1<<(offset_size*8);
						lcn+=offset;
						if(lcn>=0 && static_cast<int64>(length)>=0 && static_cast<int64>(length)<=total_clusters-lcn)
						{
							fs->excludeSectors(lcn, length);
							excluded_bytes+=length*fs->getBlocksize();
						}
					}
					p+=1+length_size+offset_size;
				}
			}
		}

		virtual void record(uint64 recno, const char* data, const NTFSFileRecord& header)
		{
			if(header.flags & mft_record_directory)
				return;
			if(header.base_record!=0)
			{
				//Extension record of a file with an attribute list: its data belongs to the base record
				uint64 base=header.base_record & mft_ref_mask;
				if(excluded_bases.find(base)!=excluded_bases.end())
					excludeData(data, header);
				else if(base>recno)
					pending_extensions.push_back(recno);
				return;
			}
			if(!isExcluded(data, header))
				return;
			++n_excluded;
			excludeData(data, header);
			unsigned int pos=header.attribute_offset;
			MFTAttribute attr;
			for(;nextAttribute(data, header.real_size, pos, attr);pos+=attr.length)
			{
				if(attr.type==mft_attr_attribute_list)
					excluded_bases.insert(recno);
			}
		}

		Filesystem* fs;
		std::string root;
		const std::map<uint64, SDirEntry>& dirs;
		IFsExcludeCallback* callback;
		int64 total_clusters;
		std::map<uint64, std::string> paths;
		std::set<uint64> excluded_bases;
		std::vector<uint64> pending_extensions;
		int64 excluded_bytes;
		size_t n_excluded;
	} file_visitor(target, volume_root, dir_visitor.dirs, callback);

	if(!walkMft(file_visitor))
		return -1;

	//Extension records that came before their base record
	uint64 mftclusters=(mftsize+clustersize-1)/clustersize;
	std::vector<char> buf(static_cast<size_t>((mftrecordsize/clustersize+2)*clustersize));
	for(size_t i=0;i<file_visitor.pending_extensions.size();++i)
	{
		uint64 recno=file_visitor.pending_extensions[i];
		uint64 vcn=(recno*mftrecordsize)/clustersize;
		if(!readMftClusters(vcn, (std::min)(static_cast<uint64>(buf.size()/clustersize), mftclusters-vcn), buf.data()))
			continue;
		char* rec=buf.data()+(recno*mftrecordsize)%clustersize;
		NTFSFileRecord header;
		memcpy(&header, rec, sizeof(NTFSFileRecord));
		if(file_visitor.excluded_bases.find(header.base_record & mft_ref_mask)!=file_visitor.excluded_bases.end()
			&& applyFixups(rec, mftrecordsize, rec+header.sequence_offset, header.sequence_size*2))
		{
			file_visitor.excludeData(rec, header);
		}
	}

	Server->Log("Excluded "+convert(file_visitor.n_excluded)+" files from the image of "+volume_root, LL_DEBUG);
	return file_visitor.excluded_bytes;
}

bool FSNTFS::checkMFTMirror(unsigned int mftrecordsize, Runlist &mftrunlist, NTFSFileRecord &mft, bool fix)
{
	unsigned int mirr_vcn=(1*mftrecordsize)/clustersize;
	uint64 mirr_lcn=mftrunlist.getLCN(mirr_vcn);
	if(mirr_lcn==UD_UINT64)
	{
		Server->Log("Error mapping VCN to LCN", LL_ERROR);
		return false;
	}
	uint64 mirr_pos=mirr_lcn*clustersize+(1*mftrecordsize)%clustersize;
	char *mirrrecord=new char[mftrecordsize];
	MemFree mirrrecord_free(mirrrecord);

	_u32 rc=sectorRead(mirr_pos, mirrrecord, mftrecordsize);
	if(rc==0)
	{
		Server->Log("Error reading bitmap MFT entry", LL_DEBUG);
		return false;
	}

	NTFSFileRecord mftmirr;
	memcpy((char*)&mftmirr, mirrrecord, sizeof(NTFSFileRecord) );

	if(!applyFixups(mirrrecord, mftrecordsize, mirrrecord+mftmirr.sequence_offset, mftmirr.sequence_size*2 ) )
	{
		Server->Log("Applying fixups failed", LL_ERROR);
		return false;
	}
	
	if(mftmirr.magic[0]!='F' || mftmirr.magic[1]!='I' || mftmirr.magic[2]!='L' || mftmirr.magic[3]!='E' )
	{
		has_error=true;
		return false;
	}

	_u32 currpos=0;
	MFTAttribute attr;
	attr.length=mft.attribute_offset;
	bool is_mftmirr=false;
	do
	{
		currpos+=attr.length;
		memcpy((char*)&attr, mirrrecord+currpos, sizeof(MFTAttribute) );
		if(attr.type==0x30 && attr.nonresident==0) //FILENAME
		{
			MFTAttributeFilename fn;
			memcpy((char*)&fn, mirrrecord+currpos+attr.attribute_offset, sizeof(MFTAttributeFilename) );
			std::string fn_uc;
			fn_uc.resize(fn.filename_length*2);
			memcpy(&fn_uc[0], mirrrecord+currpos+attr.attribute_offset+sizeof(MFTAttributeFilename), fn.filename_length*2);
			Server->Log("Filename="+Server->ConvertFromUTF16(fn_uc) , LL_DEBUG);
			if(Server->ConvertFromUTF16(fn_uc)=="$MFTMirr")
			{
				is_mftmirr=true;
			}
		}
		Server->Log("Attribute Type: "+convert(attr.type)+" nonresident="+convert(attr.nonresident)+" length="+convert(attr.length), LL_DEBUG);
	}while( attr.type!=0xFFFFFFFF && attr.type!=0x80);

	if(!is_mftmirr)
	{
		Server->Log("Filename attribute not found or filename wrong", LL_ERROR);
		return false;
	}

	if(attr.type!=0x80)
	{
		Server->Log("Data Attribute of MftMirr not found", LL_ERROR);
		return false;
	}

	if(attr.nonresident!=1)
	{
		Server->Log("DATA is resident!! - unexpected -2", LL_ERROR);
		return false;
	}

	MFTAttributeNonResident mftmirrstream;
	memcpy((char*)&mftmirrstream, mirrrecord+currpos, sizeof(MFTAttributeNonResident) );
	if(mftmirrstream.compression_size!=0)
	{
		Server->Log("MFT Rundata is compressed. Can't handle that. -2", LL_ERROR);
		return false;
	}

	Runlist mirrrunlist(mirrrecord+currpos+mftmirrstream.run_offset );
	
	unsigned char *mftmirr_data=new unsigned char[(unsigned int)mftmirrstream.real_size];
	MemFree mftmirr_data_free(reinterpret_cast<char*>(mftmirr_data));
	char *buffer=new char[clustersize];
	MemFree buffer_free(buffer);
	mirr_pos=0;
	for(uint64 i=mftmirrstream.starting_vnc;i<=mftmirrstream.last_vnc;++i)
	{
		uint64 lcn=mirrrunlist.getLCN(i);
		if(lcn==UD_UINT64)
		{
			Server->Log("Error mapping VCN->LCN. -2", LL_ERROR);
			return false;
		}
		dev->Seek(lcn*clustersize);
		rc=dev->Read(buffer, clustersize);
		if(rc!=clustersize)
		{
			Server->Log("Error reading cluster "+convert(lcn)+" code: 529", LL_ERROR);
			return false;
		}
		memcpy(&mftmirr_data[mirr_pos], buffer, (size_t)(std::min)(mftmirrstream.real_size-mirr_pos, (uint64)clustersize) );
		mirr_pos+=(std::min)(mftmirrstream.real_size-mirr_pos, (uint64)clustersize);
	}

	bool has_fix=false;

	for(uint64 i=0;i<mftmirrstream.real_size/mftrecordsize;++i)
	{
		uint64 currfile_vcn=(i*mftrecordsize)/clustersize;
		uint64 currfile_lcn=mftrunlist.getLCN(currfile_vcn);
		if(currfile_lcn==UD_UINT64)
		{
			Server->Log("Error mapping VCN to LCN", LL_ERROR);
			return false;
		}
		uint64 currfile_pos=currfile_lcn*clustersize+(1*mftrecordsize)%clustersize;
		char *currfilerecord=new char[mftrecordsize];
		MemFree currfilerecord_free(currfilerecord);

		_u32 rc=sectorRead(currfile_pos, currfilerecord, mftrecordsize);
		if(rc==0)
		{
			Server->Log("Error reading currfile MFT entry", LL_DEBUG);
			return false;
		}

		NTFSFileRecord *A=(NTFSFileRecord *)currfilerecord;
		NTFSFileRecord *B=(NTFSFileRecord *)(mftmirr_data+i*mftrecordsize);

		if(A->lsn!=B->lsn)
		{
			Server->Log("MFT file record in MFT mirror differs in file record "+convert(i)+": Logfile sequence number differs", LL_WARNING);
		}

		if(memcmp(currfilerecord, mftmirr_data+i*mftrecordsize, mftrecordsize)!=0)
		{
			Server->Log("MFT file record in MFT mirror differs in file record "+convert(i), LL_WARNING);
			if(fix)
			{
				memcpy(mftmirr_data+i*mftrecordsize, currfilerecord, mftrecordsize);
				has_fix=true;
			}
			else
			{
				return false;
			}
		}
	}

	if(has_fix)
	{
		mirr_pos=0;
		for(uint64 i=mftmirrstream.starting_vnc;i<=mftmirrstream.last_vnc;++i)
		{
			uint64 lcn=mirrrunlist.getLCN(i);
			if(lcn==UD_UINT64)
			{
				Server->Log("Error mapping VCN->LCN. -2", LL_ERROR);
				return false;
			}
			dev->Seek(lcn*clustersize);
			rc=dev->Write((const char*)(mftmirr_data+i*clustersize), clustersize);
			if(rc!=clustersize)
			{
				Server->Log("Error writing cluster "+convert(lcn)+" code: 652", LL_WARNING);
				return false;
			}
		}

		Server->Log("Fixed MFT mirror", LL_ERROR);
	}

	return true;
}

//-------------- RUNLIST -----------------

Runlist::Runlist(char *pData) : data(pData)
{
	reset();
}

void Runlist::reset(void)
{
	pos=data;
}

bool Runlist::getNext(RunlistItem &item)
{
	char f=*pos;
	if(f==0)
		return false;

	char offset_size=f >> 4;
	char length_size=f &  0x0F;
	item.length=0;
	item.offset=0;
	memcpy(&item.length, pos+1, length_size);

	bool is_signed=(*(pos+1+length_size+offset_size-1) & 0x80)>0;
	memcpy(&item.offset, pos+1+length_size, offset_size);

	if(is_signed)
	{
		char * ar=(char*)&item.offset;
		ar[offset_size-1]=ar[offset_size-1] & 0x7F;
		item.offset*=-1;
	}

	pos+=1+offset_size+length_size;
	return true;
}

uint64 Runlist::getSizeInClusters(void)
{
	reset();
	RunlistItem item;
	uint64 size=0;
	while(getNext(item))
	{
		size+=item.length;
	}
	return size;
}

uint64 Runlist::getLCN(uint64 vcn)
{
	reset();
	RunlistItem item;
	uint64 lcn=0;
	uint64 coffset=0;
	while(getNext(item))
	{
		lcn+=item.offset;

		if(coffset<=vcn && coffset+item.length>vcn )
		{
			return lcn+(vcn-coffset);
		}

		coffset+=item.length;
	}
	return UD_UINT64;
}

