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

#include "../../Interface/Server.h"
#include "../../stringtools.h"
#include "ntfs.h"
#include <math.h>

FSNTFS::FSNTFS(const std::wstring &pDev) : Filesystem(pDev)
{
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

	Server->Log("Sectorsize: "+nconvert(sectorsize), LL_DEBUG);
	Server->Log("Clustersize: "+nconvert(clustersize), LL_DEBUG);
	Server->Log("ClustersPerMFTNode Offset: "+nconvert((unsigned int)&br.mftlcn-(unsigned int)&br), LL_DEBUG);

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
	Server->Log("MFTStart: "+nconvert(br.mftlcn), LL_DEBUG);

	char *mftrecord=new char[mftrecordsize];

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
			Server->Log(L"Filename="+Server->ConvertFromUTF16(fn_uc) , LL_DEBUG);
		}
		Server->Log("Attribute Type: "+nconvert(attr.type)+" nonresident="+nconvert(attr.nonresident)+" length="+nconvert(attr.length), LL_DEBUG);
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

	unsigned int bitmap_vcn=(6*mftrecordsize)/clustersize;
	uint64 bitmap_lcn=mftrunlist.getLCN(bitmap_vcn);
	if(bitmap_lcn==0xFFFFFFFFFFFFFFFF)
	{
		Server->Log("Error mapping VCN to LCN", LL_ERROR);
		has_error=true;
		return;
	}
	uint64 bitmap_pos=bitmap_lcn*clustersize+(6*mftrecordsize)%clustersize;
	char *bitmaprecord=new char[mftrecordsize];
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
			Server->Log(L"Filename="+Server->ConvertFromUTF16(fn_uc) , LL_DEBUG);
		}
		Server->Log("Attribute Type: "+nconvert(attr.type)+" nonresident="+nconvert(attr.nonresident)+" length="+nconvert(attr.length), LL_DEBUG);
	}while( attr.type!=0xFFFFFFFF && attr.type!=0x80);

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
	
	bitmap=new unsigned char[(unsigned int)bitmapstream.real_size];
	char *buffer=new char[clustersize];
	bitmap_pos=0;
	for(uint64 i=bitmapstream.starting_vnc;i<=bitmapstream.last_vnc;++i)
	{
		uint64 lcn=bitmaprunlist.getLCN(i);
		if(lcn==0xFFFFFFFFFFFFFFFF)
		{
			Server->Log("Error mapping VCN->LCN. -2", LL_ERROR);
			has_error=true;
			return;
		}
		dev->Seek(lcn*clustersize);
		rc=dev->Read(buffer, clustersize);
		if(rc!=clustersize)
		{
			Server->Log("Error reading cluster "+nconvert(lcn)+" code: 529", LL_ERROR);
			has_error=true;
			return;
		}
		memcpy(&bitmap[bitmap_pos], buffer, (size_t)(std::min)(bitmapstream.real_size-bitmap_pos, (uint64)clustersize) );
		bitmap_pos+=(std::min)(bitmapstream.real_size-bitmap_pos, (uint64)clustersize);
	}

	delete []buffer;
	delete []mftrecord;
	delete []bitmaprecord;
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
	return 0xFFFFFFFFFFFFFFFF;
}