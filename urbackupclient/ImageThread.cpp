/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"

#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IFilesystem.h"

#include "../common/data.h"
#include "../urbackupcommon/sha2/sha2.h"

#include "ClientService.h"
#include "ImageThread.h"
#include "ClientSend.h"
#include "client.h"

#include <memory.h>
#include <stdlib.h>
#include <assert.h>
#include <memory>

extern IFSImageFactory *image_fak;

namespace
{
	const unsigned char ImageFlag_Persistent=1;
	const unsigned char ImageFlag_Bitmap=2;
}


ImageThread::ImageThread(ClientConnector *client, IPipe *pipe, IPipe *mempipe, ImageInformation *image_inf, std::string server_token, IFile *hashdatafile)
	: client(client), pipe(pipe), mempipe(mempipe), image_inf(image_inf), server_token(server_token), hashdatafile(hashdatafile)
{
}

void ImageThread::ImageErr(const std::string &msg, int loglevel)
{
	Server->Log(msg, loglevel);
#ifdef _WIN32
	uint64 bs=0xFFFFFFFFFFFFFFFF;
#else
	uint64 bs=0xFFFFFFFFFFFFFFFFLLU;
#endif
	char *buffer=new char[sizeof(uint64)+msg.size()];
	memcpy(buffer, &bs, sizeof(uint64) );
	memcpy(&buffer[sizeof(uint64)], msg.c_str(), msg.size());
	pipe->Write(buffer, sizeof(uint64)+msg.size());
	delete [] buffer;
}

void ImageThread::ImageErrRunning(std::string msg)
{
	Server->Log(msg, LL_ERROR);
	msg+="|#|";
	int64 bs=-124;
	char *buffer=new char[sizeof(int64)+msg.size()];
	memcpy(buffer, &bs, sizeof(int64) );
	memcpy(&buffer[sizeof(int64)], msg.c_str(), msg.size());
	pipe->Write(buffer, sizeof(int64)+msg.size());
	delete [] buffer;
}

const unsigned int c_vhdblocksize=(1024*1024/2);
const unsigned int c_hashsize=32;

void ImageThread::sendFullImageThread(void)
{
	bool has_error=true;

	int save_id=-1;
	bool with_checksum=image_inf->with_checksum;

	int64 last_shadowcopy_update = Server->getTimeSeconds();

	bool run=true;
	while(run)
	{
		if(image_inf->shadowdrive.empty() && !image_inf->no_shadowcopy)
		{
			std::string msg;
			mempipe->Read(&msg);
			if(msg.find("done")==0)
			{
				image_inf->shadowdrive=getafter("-", msg);
				save_id=atoi(getuntil("-", image_inf->shadowdrive).c_str());
				image_inf->shadowdrive=getafter("-", image_inf->shadowdrive);
				if(image_inf->shadowdrive.size()>0 && image_inf->shadowdrive[image_inf->shadowdrive.size()-1]=='\\')
					image_inf->shadowdrive.erase(image_inf->shadowdrive.begin()+image_inf->shadowdrive.size()-1);
				if(image_inf->shadowdrive.empty())
				{
					ImageErr("Creating Shadow drive failed. Stopping. -2");
					run=false;
				}
			}
			else
			{
				ImageErr("Creating Shadow drive failed. Stopping.");
				run=false;
				/*image_inf->shadowdrive="\\\\.\\" + image_inf->image_letter;
				image_inf->no_shadowcopy=true;*/
			}
			mempipe->Write("exit");
			mempipe=NULL;
		}
		else
		{
			std::auto_ptr<IFilesystem> fs;
			FsShutdownHelper shutdown_helper;
			if(!image_inf->shadowdrive.empty())
			{
				fs.reset(image_fak->createFilesystem(Server->ConvertToUnicode(image_inf->shadowdrive), true,
					IndexThread::backgroundBackupsEnabled(std::string()), true));
				shutdown_helper.reset(fs.get());
			}
			if(fs.get()==NULL)
			{
				int tloglevel=LL_ERROR;
				if(image_inf->shadowdrive.empty())
				{
					tloglevel=LL_INFO;
				}
				ImageErr("Opening filesystem on device failed. Stopping.", tloglevel);
				Server->Log("Device file: \""+image_inf->shadowdrive+"\"", LL_INFO);
				run=false;
				break;
			}

			unsigned int blocksize=(unsigned int)fs->getBlocksize();
			int64 drivesize=fs->getSize();
			int64 blockcnt=fs->calculateUsedSpace()/blocksize;
			int64 ncurrblocks=0;
			sha256_ctx shactx;
			unsigned char *zeroblockbuf=NULL;
			unsigned int vhdblocks=c_vhdblocksize/blocksize;

			if(with_checksum)
			{
				sha256_init(&shactx);
				zeroblockbuf=new unsigned char[blocksize];
				memset(zeroblockbuf, 0, blocksize);
			}

			if(image_inf->startpos==0)
			{
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int)+c_hashsize];
				char *cptr=buffer;
				memcpy(cptr, &blocksize, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &drivesize, sizeof(int64) );
				cptr+=sizeof(int64);
				memcpy(cptr, &blockcnt, sizeof(int64) );
				cptr+=sizeof(int64);
				unsigned char* flags = reinterpret_cast<unsigned char*>(cptr);
				*flags=0;
	#ifndef VSS_XP //persistence
	#ifndef VSS_S03
				*flags|=ImageFlag_Persistent;
	#endif
	#endif
				if(image_inf->no_shadowcopy)
				{
					*flags = *flags & (^ImageFlag_Persistent);
				}
				if(image_inf->with_bitmap)
				{
					*flags|=ImageFlag_Bitmap;
				}
				++cptr;
				unsigned int shadowdrive_size=(unsigned int)image_inf->shadowdrive.size();
				memcpy(cptr, &shadowdrive_size, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &image_inf->shadowdrive[0], image_inf->shadowdrive.size());
				cptr+=image_inf->shadowdrive.size();
				memcpy(cptr, &save_id, sizeof(int));
				cptr+=sizeof(int);
				size_t bsize=sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int);
				if(with_checksum)
				{
					sha256_update(&shactx, (unsigned char*)buffer, (unsigned int)bsize);
					sha256_final(&shactx, (unsigned char*)cptr);
					bsize+=c_hashsize;
					sha256_init(&shactx);
				}
				bool b=pipe->Write(buffer, bsize );
				delete []buffer;
				if(!b)
				{
					Server->Log("Pipe broken -1", LL_ERROR);
					run=false;
					break;
				}
			}
			
			
			if(image_inf->with_bitmap)
			{
				size_t bitmap_size = (drivesize/blocksize)/8;
				if( (drivesize/blocksize)%8!=0 )
				{
					++bitmap_size;
				}
				
				unsigned char* bitmap = fs->getBitmap();
				
				sha256_ctx shactx;
				sha256_innit(&shactx);
				
				while(bitmap_size>0)
				{
					size_t tosend = (std::min)((size_t)4096, bitmap_size);
					
					sha256_update(&shactx, bitmap, (unsigned int)tosend);
					
					if(!pipe->Write(reinterpret_cast<char*>(bitmap), tosend))
					{
						Server->Log("Pipe broken while sending bitmap", LL_ERROR);
						run = false;
						break;
					}
					
					bitmap_size-=tosend;
					bitmap+=tosend;
				}
				
				unsigned char dig[c_hashsize];
				sha256_final(&shactx, dig);
				
				if(!pipe->Write(reinterpret_cast<char*>(dig), c_hashsize))
				{
					Server->Log("Pipe broken while sending bitmap checksum", LL_ERROR);
					run = false;
					break;
				}
			}
			
			ClientSend *cs=new ClientSend(pipe, blocksize+sizeof(int64), 2000);
			THREADPOOL_TICKET send_ticket=Server->getThreadPool()->execute(cs);

			unsigned int needed_bufs=64;
			int64 last_hash_block=-1;
			std::vector<char*> bufs;
			for(int64 i=image_inf->startpos,blocks=drivesize/blocksize;i<blocks;i+=64)
			{
				std::vector<char*> n_bufs=cs->getBuffers(needed_bufs);
				bufs.insert(bufs.end(), n_bufs.begin(), n_bufs.end() );
				needed_bufs=0;

				unsigned int n=(unsigned int)(std::min)(blocks-i, (int64)64);
				std::vector<int64> secs=fs->readBlocks(i, n, bufs, sizeof(int64));
				if(fs->hasError())
				{
					ImageErrRunning("Error while reading from shadow copy device -1");
					run=false;
					has_error=true;
					break;
				}
				needed_bufs+=(_u32)secs.size();
				bool notify_cs=false;
				if(with_checksum)
				{
					size_t idx=0;
					size_t n_secs=secs.size();
					for(int64 j=i;j<i+64 && j<blocks;++j)
					{
						if(idx<n_secs && secs[idx]==j)
						{
							if( last_hash_block/vhdblocks<j/vhdblocks)
							{
								last_hash_block=(j/vhdblocks)*vhdblocks-1;								
							}

							for(int64 k=last_hash_block+1;k<j;++k)
							{
								sha256_update(&shactx, zeroblockbuf, blocksize);
							}

							memcpy(bufs[idx], &secs[idx], sizeof(int64) );
							sha256_update(&shactx, (unsigned char*)bufs[idx]+sizeof(int64), blocksize);
							cs->sendBuffer(bufs[idx], sizeof(int64)+blocksize, false);
							++idx;
							notify_cs=true;
							last_hash_block=j;
						}

						if( (j+1)%vhdblocks==0 || j+1==blocks )
						{
							if(last_hash_block>=j+1-vhdblocks )
							{
								for(int64 k=last_hash_block;k<j;++k)
								{
									sha256_update(&shactx, zeroblockbuf, blocksize);
								}

								unsigned char dig[c_hashsize];
								sha256_final(&shactx, dig);							
								char* cb=cs->getBuffer();
								int64 bs=-126;
								int64 nextblock=j+1;
								memcpy(cb, &bs, sizeof(int64) );
								memcpy(cb+sizeof(int64), &nextblock, sizeof(int64));
								memcpy(cb+2*sizeof(int64), dig, c_hashsize);
								cs->sendBuffer(cb, 2*sizeof(int64)+c_hashsize, false);
								notify_cs=true;
							}
							sha256_init(&shactx);
						}
					}
				}
				else
				{
					for(size_t j=0;j<secs.size();++j)
					{
						memcpy(bufs[j], &secs[j], sizeof(int64) );
						cs->sendBuffer(bufs[j], sizeof(int64)+blocksize, false);
						notify_cs=true;
					}
				}
				if(notify_cs)
				{
					cs->notifySendBuffer();
				}

				if(!secs.empty())
					bufs.erase(bufs.begin(), bufs.begin()+secs.size() );
				if(cs->hasError() )
				{
					Server->Log("Pipe broken -2", LL_ERROR);
					run=false;
					has_error=true;
					break;
				}

				ncurrblocks+=secs.size();
				
				if(!secs.empty() && IdleCheckerThread::getPause())
				{
					Server->wait(30000);
				}

				if(Server->getTimeSeconds() - last_shadowcopy_update > 1*60*60)
				{
					updateShadowCopyStarttime(save_id);
					last_shadowcopy_update = Server->getTimeSeconds();
				}
			}

			for(size_t i=0;i<bufs.size();++i)
			{
				cs->freeBuffer(bufs[i]);
			}
			cs->doExit();
			std::vector<THREADPOOL_TICKET> wf;
			wf.push_back(send_ticket);
			Server->getThreadPool()->waitFor(wf);
			delete cs;

			if(!run)break;

			char lastbuffer[sizeof(int64)];
			int64 lastn=-123;
			memcpy(lastbuffer,&lastn, sizeof(int64));
			bool b=pipe->Write(lastbuffer, sizeof(int64));
			if(!b)
			{
				Server->Log("Pipe broken -3", LL_ERROR);
				run=false;
				has_error=true;
				break;
			}

			has_error=false;

			run=false;
		}
	}

	Server->Log("Sending full image done", LL_INFO);

#ifdef VSS_XP //persistence
	has_error=false;
#endif
#ifdef VSS_S03
	has_error=false;
#endif

	if(!has_error && !image_inf->no_shadowcopy)
	{
		removeShadowCopyThread(save_id);
	}
	client->doQuitClient();
}

void ImageThread::removeShadowCopyThread(int save_id)
{
	if(!image_inf->no_shadowcopy)
	{
		IPipe* local_pipe=Server->createMemoryPipe();

		CWData data;
		data.addChar(3);
		data.addVoidPtr(local_pipe);
		data.addString(image_inf->image_letter);
		data.addString(server_token);
		data.addUChar(1);
		data.addInt(save_id);

		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

		std::string msg;
		local_pipe->Read(&msg);
		if(msg.find("done")!=0)
		{
			Server->Log("Removing shadow copy failed in image streaming: "+msg, LL_ERROR);
		}
		local_pipe->Write("exit");
	}
}

void ImageThread::sendIncrImageThread(void)
{
	char *zeroblockbuf=NULL;
	std::vector<char*> blockbufs;

	bool has_error=true;
	bool with_checksum=image_inf->with_checksum;
	bool with_emptyblocks=image_inf->with_emptyblocks;

	int save_id=-1;
	int update_cnt=0;

	int64 lastsendtime=Server->getTimeMS();
	int64 last_shadowcopy_update = Server->getTimeSeconds();

	char* empty_vhd_blocks_buffer = NULL;
	size_t empty_vhd_block_buffer_pos = 0;

	bool run=true;
	while(run)
	{
		if(image_inf->shadowdrive.empty())
		{
			std::string msg;
			mempipe->Read(&msg);
			if(msg.find("done")==0)
			{
				image_inf->shadowdrive=getafter("-", msg);
				save_id=atoi(getuntil("-", image_inf->shadowdrive).c_str());
				image_inf->shadowdrive=getafter("-", image_inf->shadowdrive);
				if(image_inf->shadowdrive.size()>0 && image_inf->shadowdrive[image_inf->shadowdrive.size()-1]=='\\')
					image_inf->shadowdrive.erase(image_inf->shadowdrive.begin()+image_inf->shadowdrive.size()-1);
				if(image_inf->shadowdrive.empty())
				{
					ImageErr("Creating Shadow drive failed. Stopping. -2");
					run=false;
				}
			}
			else
			{
				ImageErr("Creating Shadow drive failed. Stopping.");
				run=false;
			}
			mempipe->Write("exit");
			mempipe=NULL;
		}
		else
		{
			std::auto_ptr<IFilesystem> fs;
			FsShutdownHelper shutdown_helper;
			if(!image_inf->shadowdrive.empty())
			{
				fs.reset(image_fak->createFilesystem(Server->ConvertToUnicode(image_inf->shadowdrive), true,
					IndexThread::backgroundBackupsEnabled(std::string()), true));
				shutdown_helper.reset(fs.get());
			}
			if(fs.get()==NULL)
			{
				int tloglevel=LL_ERROR;
				if(image_inf->shadowdrive.empty())
				{
					tloglevel=LL_INFO;
				}
				ImageErr("Opening filesystem on device failed. Stopping.", tloglevel);
				Server->Log("Device file: \""+image_inf->shadowdrive+"\"", LL_INFO);
				run=false;
				break;
			}

			sha256_ctx shactx;
			unsigned int vhdblocks;

			unsigned int blocksize=(unsigned int)fs->getBlocksize();
			int64 drivesize=fs->getSize();
			int64 blockcnt=fs->calculateUsedSpace()/blocksize;
			vhdblocks=c_vhdblocksize/blocksize;
			int64 currvhdblock=0;
			int64 numblocks=drivesize/blocksize;

			if(image_inf->startpos==0)
			{
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int)+c_hashsize];
				char *cptr=buffer;
				memcpy(cptr, &blocksize, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &drivesize, sizeof(int64) );
				cptr+=sizeof(int64);
				memcpy(cptr, &blockcnt, sizeof(int64) );
				cptr+=sizeof(int64);
	#ifndef VSS_XP //persistence
	#ifndef VSS_S03
				*cptr=1;
	#else
				*cptr=0;
	#endif
	#else
				*cptr=0;
	#endif
				++cptr;
				unsigned int shadowdrive_size=(unsigned int)image_inf->shadowdrive.size();
				memcpy(cptr, &shadowdrive_size, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &image_inf->shadowdrive[0], image_inf->shadowdrive.size());
				cptr+=image_inf->shadowdrive.size();
				memcpy(cptr, &save_id, sizeof(int));
				cptr+=sizeof(int);
				size_t bsize=sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int);
				if(with_checksum)
				{
					sha256_init(&shactx);
					sha256_update(&shactx, (unsigned char*)buffer, (unsigned int)bsize);
					sha256_final(&shactx, (unsigned char*)cptr);
					bsize+=c_hashsize;
					sha256_init(&shactx);
				}
				bool b=pipe->Write(buffer, bsize);
				delete []buffer;
				if(!b)
				{
					Server->Log("Pipe broken -1", LL_ERROR);
					run=false;
					break;
				}
			}
			
			blockbufs.clear();
			blockbufs.resize(vhdblocks);
			delete []zeroblockbuf;
			zeroblockbuf=new char[blocksize];
			memset(zeroblockbuf, 0, blocksize);

			ClientSend *cs=new ClientSend(pipe, blocksize+sizeof(int64), 2000);
			THREADPOOL_TICKET send_ticket=Server->getThreadPool()->execute(cs);


			for(int64 i=image_inf->startpos,blocks=drivesize/blocksize;i<blocks;i+=vhdblocks)
			{
				++update_cnt;
				if(update_cnt>10)
				{
					client->updatePCDone2((int)(((float)i/(float)blocks)*100.f+0.5f));
					update_cnt=0;
				}
				currvhdblock=i/vhdblocks;
				bool has_data=false;
				for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
				{
					if( fs->hasBlock(j) )
					{
						has_data=true;
						break;
					}
				}

#ifdef _DEBUG
				for(size_t k=0;k<blockbufs.size();++k)
				{
					assert(blockbufs[k]==NULL);
				}
#endif

				if(has_data)
				{
					sha256_init(&shactx);
					bool mixed=false;
					for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
					{
						char* buf = fs->readBlock(j);
						if( buf!=NULL )
						{
							sha256_update(&shactx, (unsigned char*)buf, blocksize);
							blockbufs[j-i] = buf;
						}
						else
						{
							sha256_update(&shactx, (unsigned char*)zeroblockbuf, blocksize);
							mixed=true;
							blockbufs[j-i] = NULL;
						}
					}
					if(fs->hasError())
					{
						for(size_t i=0;i<blockbufs.size();++i)
						{
							if(blockbufs[i]!=NULL)
							{
								fs->releaseBuffer(blockbufs[i]);
								blockbufs[i]=NULL;
							}
						}
						ImageErrRunning("Error while reading from shadow copy device -2");
						run=false;
						break;
					}
					unsigned char digest[c_hashsize];
					sha256_final(&shactx, digest);
					bool has_hashdata=false;
					char hashdata_buf[c_hashsize];
					if(hashdatafile->Size()>=(currvhdblock+1)*c_hashsize)
					{
						hashdatafile->Seek(currvhdblock*c_hashsize);						
						if( hashdatafile->Read(hashdata_buf, c_hashsize)!=c_hashsize )
						{
							Server->Log("Reading hashdata failed!", LL_ERROR);
						}
						else
						{
							has_hashdata=true;
						}
					}						
					if(!has_hashdata || memcmp(hashdata_buf, digest, c_hashsize)!=0)
					{
						if(empty_vhd_blocks_buffer!=NULL)
						{
							cs->sendBuffer(empty_vhd_blocks_buffer, empty_vhd_block_buffer_pos, true);
							empty_vhd_blocks_buffer=NULL;
						}

						Server->Log("Block did change: "+nconvert(i)+" mixed="+nconvert(mixed), LL_DEBUG);
						bool notify_cs=false;
						for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
						{
							if(blockbufs[j-i]!=NULL)
							{
								char* cb=cs->getBuffer();
								memcpy(cb, &j, sizeof(int64) );
								memcpy(&cb[sizeof(int64)], blockbufs[j-i], blocksize);
								cs->sendBuffer(cb, sizeof(int64)+blocksize, false);
								notify_cs=true;
								lastsendtime=Server->getTimeMS();
								fs->releaseBuffer(blockbufs[j-i]);
								blockbufs[j-i]=NULL;
							}
						}

						if(notify_cs)
						{
							cs->notifySendBuffer();
							if(cs->hasError())
							{
								Server->Log("Pipe broken -2", LL_ERROR);
								run=false;
							}
						}

						if(with_checksum)
						{
							char* cb=cs->getBuffer();
							int64 bs=-126;
							int64 nextblock=(std::min)(blocks, i+vhdblocks);
							memcpy(cb, &bs, sizeof(int64) );
							memcpy(cb+sizeof(int64), &nextblock, sizeof(int64));
							memcpy(cb+2*sizeof(int64), digest, c_hashsize);
							cs->sendBuffer(cb, 2*sizeof(int64)+c_hashsize, true);
							//Server->Log("Block hash="+base64_encode(digest, c_hashsize), LL_DEBUG);
						}
					}
					else
					{
						//Server->Log("Block didn't change: "+nconvert(i), LL_DEBUG);
						int64 tt=Server->getTimeMS();
						if(tt-lastsendtime>10000)
						{
							int64 bs=-125;
							char* buffer=cs->getBuffer();
							memcpy(buffer, &bs, sizeof(int64) );
							cs->sendBuffer(buffer, sizeof(int64), true);

							lastsendtime=tt;
						}

						for(size_t k=0;k<blockbufs.size();++k)
						{
							if(blockbufs[k]!=NULL)
							{
								fs->releaseBuffer(blockbufs[k]);
								blockbufs[k]=NULL;
							}
						}
					}
					if(!run)break;
				}
				else
				{
					int64 tt=Server->getTimeMS();
					if(tt-lastsendtime>10000)
					{
						int64 bs=-125;
						char* buffer=cs->getBuffer();
						memcpy(buffer, &bs, sizeof(int64) );
						cs->sendBuffer(buffer, sizeof(int64), true);

						lastsendtime=tt;
					}

					if(empty_vhd_blocks_buffer==NULL)
					{
						empty_vhd_blocks_buffer = cs->getBuffer();
						empty_vhd_block_buffer_pos = 0;
					}

					if(with_emptyblocks)
					{
						int64 bs=-127;
						memcpy(&empty_vhd_blocks_buffer[empty_vhd_block_buffer_pos], &bs, sizeof(bs));
						empty_vhd_block_buffer_pos+=sizeof(bs);
						memcpy(&empty_vhd_blocks_buffer[empty_vhd_block_buffer_pos], &i, sizeof(i));
						empty_vhd_block_buffer_pos+=sizeof(i);

						if(empty_vhd_block_buffer_pos>=blocksize-sizeof(int64))
						{
							cs->sendBuffer(empty_vhd_blocks_buffer, empty_vhd_block_buffer_pos, true);
							empty_vhd_blocks_buffer=NULL;
						}
					}
				}

				if(IdleCheckerThread::getPause())
				{
					Server->wait(30000);
				}

				if(Server->getTimeSeconds() - last_shadowcopy_update > 1*60*60)
				{
					updateShadowCopyStarttime(save_id);
					last_shadowcopy_update = Server->getTimeSeconds();
				}
			}

			if(empty_vhd_blocks_buffer!=NULL)
			{
				cs->sendBuffer(empty_vhd_blocks_buffer, empty_vhd_block_buffer_pos, true);
				empty_vhd_blocks_buffer=NULL;
			}

			cs->doExit();
			std::vector<THREADPOOL_TICKET> wf;
			wf.push_back(send_ticket);
			Server->getThreadPool()->waitFor(wf);
			delete cs;

			if(!run)break;

			char lastbuffer[sizeof(int64)];
			int64 lastn=-123;
			memcpy(lastbuffer,&lastn, sizeof(int64));
			bool b=pipe->Write(lastbuffer, sizeof(int64));
			if(!b)
			{
				Server->Log("Pipe broken -3", LL_ERROR);
				run=false;
				break;
			}

			has_error=false;

			run=false;
		}
	}

	delete [] zeroblockbuf;

	std::wstring hashdatafile_fn=hashdatafile->getFilenameW();
	Server->destroy(hashdatafile);
	Server->deleteFile(hashdatafile_fn);

	Server->Log("Sending image done", LL_INFO);

#ifdef VSS_XP //persistence
	has_error=false;
#endif
#ifdef VSS_S03
	has_error=false;
#endif

	if(!has_error)
	{
		removeShadowCopyThread(save_id);
	}
	client->doQuitClient();
}

void ImageThread::operator()(void)
{
#ifdef _WIN32
#ifndef _DEBUG
	if(IndexThread::backgroundBackupsEnabled())
	{
#ifdef THREAD_MODE_BACKGROUND_BEGIN
#if defined(VSS_XP) || defined(VSS_S03)
		SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#else
		SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
#else
		SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif //THREAD_MODE_BACKGROUND_BEGIN
	}
#endif _DEBUG
#endif
	if(image_inf->thread_action==TA_FULL_IMAGE)
	{
		sendFullImageThread();
	}
	else if(image_inf->thread_action==TA_INCR_IMAGE)
	{
		int timeouts=3600;
		while(client->isHashdataOkay()==false)
		{
			Server->wait(1000);
			--timeouts;
			if(timeouts<=0 || client->isQuitting()==true )
			{
				break;
			}
		}
		if(timeouts>0 && client->isQuitting()==false )
		{
			sendIncrImageThread();
		}
		else
		{
			Server->Log("Error receiving hashdata. timouts="+nconvert(timeouts)+" isquitting="+nconvert(client->isQuitting()), LL_ERROR);
			if(image_inf->shadowdrive.empty() && !image_inf->no_shadowcopy)
			{
				mempipe->Write("exit");
				mempipe=NULL;
			}
		}
	}
	client->resetImageBackupStatus(this);
#ifdef _WIN32
#ifdef THREAD_MODE_BACKGROUND_END
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
#endif
#endif
}

void ImageThread::updateShadowCopyStarttime( int save_id )
{
	if(!image_inf->no_shadowcopy)
	{
		CWData data;
		data.addChar(IndexThread::IndexThreadAction_PingShadowCopy);
		data.addVoidPtr(NULL);
		data.addString(image_inf->image_letter);
		data.addInt(save_id);

		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}
}
