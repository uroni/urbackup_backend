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

#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"

#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IFilesystem.h"

#include "../common/data.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../urbackupcommon/os_functions.h"

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
	const int initial_send_timeout = 10*60*1000;
	const int running_send_timeout = initial_send_timeout;

	const unsigned char ImageFlag_Persistent=1;
	const unsigned char ImageFlag_Bitmap=2;

	bool buf_is_zero(unsigned char* buf, size_t buf_size)
	{
		for (size_t i = 0; i < buf_size; ++i)
		{
			if (buf[i] != 0)
			{
				return false;
			}
		}
		return true;
	}
}


ImageThread::ImageThread(ClientConnector *client, IPipe *pipe, IPipe *mempipe, ImageInformation *image_inf,
	std::string server_token, IFile *hashdatafile, IFile* bitmapfile)
	: client(client), pipe(pipe), mempipe(mempipe), image_inf(image_inf), server_token(server_token),
	hashdatafile(hashdatafile), bitmapfile(bitmapfile)
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
	pipe->Write(buffer, sizeof(uint64)+msg.size(), running_send_timeout);
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
	pipe->Write(buffer, sizeof(int64)+msg.size(), running_send_timeout);
	delete [] buffer;
}

void ImageThread::createShadowData(str_map & other_vols, CWData & shadow_data)
{
	if (!other_vols.empty())
	{
		shadow_data.addChar(0);

		for (str_map::iterator it = other_vols.begin();
			it != other_vols.end(); ++it)
		{
			if (next(it->first, 0, "vol_"))
			{
				std::string idx = getafter("vol_", it->first);

				str_map::iterator it_id = other_vols.find("id_" + idx);

				if (it_id != other_vols.end())
				{
					shadow_data.addString(it->second);
					shadow_data.addVarInt(watoi(it_id->second));
				}
			}
		}
	}
}

const unsigned int c_vhdblocksize=(1024*1024/2);
const unsigned int c_hashsize=32;

bool ImageThread::sendFullImageThread(void)
{
	bool has_error=true;

	int save_id=-1;
	bool with_checksum=image_inf->with_checksum;

	int64 last_shadowcopy_update = Server->getTimeMS();

	std::auto_ptr<IFile> hdat_img;
	std::string hdat_vol;
	int r_shadow_id = -1;
	str_map other_vols;

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
				
				if (image_inf->shadowdrive.find("|") != std::string::npos)
				{
					ParseParamStrHttp(getafter("|", image_inf->shadowdrive), &other_vols);
					image_inf->shadowdrive = getuntil("|", image_inf->shadowdrive);
				}

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
				if (image_inf->shadow_id == -1)
				{
					ImageErr("Creating shadow copy failed. See client log file for details.");
				}
				else
				{
					ImageErr("Shadow copy not found. May have been deleted.");
				}
				run=false;
				/*image_inf->shadowdrive="\\\\.\\" + image_inf->image_letter;
				image_inf->no_shadowcopy=true;*/
			}
			mempipe->Write("exit");
			mempipe=NULL;

			if (run)
			{
				hdat_vol = image_inf->image_letter;
				hdat_img.reset(openHdatF(image_inf->image_letter, true));

				if (hdat_vol.size()==1)
				{
					hdat_vol += ":";
				}

				if (hdat_img.get() != NULL)
				{
					hdat_img->Read(0, reinterpret_cast<char*>(&r_shadow_id), sizeof(r_shadow_id));

					if (r_shadow_id == -1
						|| (r_shadow_id != image_inf->shadow_id
							&& r_shadow_id != save_id) )
					{
						hdat_img.reset();
					}
				}
			}
		}
		else
		{
			std::auto_ptr<IFilesystem> fs;
			FsShutdownHelper shutdown_helper;
			if(!image_inf->shadowdrive.empty())
			{
				fs.reset(image_fak->createFilesystem(image_inf->shadowdrive, IFSImageFactory::EReadaheadMode_Overlapped,
					IndexThread::backgroundBackupsEnabled(std::string()), image_inf->image_letter, this));
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

			curr_fs = fs.get();

			unsigned int blocksize=(unsigned int)fs->getBlocksize();
			int64 drivesize=fs->getSize();
			int64 blockcnt=fs->calculateUsedSpace()/blocksize;
			int64 ncurrblocks=0;
			sha256_ctx shactx;
			unsigned char *zeroblockbuf=NULL;
			unsigned int vhdblocks=c_vhdblocksize/blocksize;
			CWData shadow_data;
			createShadowData(other_vols, shadow_data);

			if(with_checksum)
			{
				sha256_init(&shactx);
				zeroblockbuf=new unsigned char[blocksize];
				memset(zeroblockbuf, 0, blocksize);
			}

			if(image_inf->startpos<0)
			{
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+ shadow_data.getDataSize()+sizeof(int)+c_hashsize];
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
				if(image_inf->with_bitmap)
				{
					*flags|=ImageFlag_Bitmap;
				}
				++cptr;
				unsigned int shadowdata_size=little_endian((unsigned int)shadow_data.getDataSize());
				memcpy(cptr, &shadowdata_size, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, shadow_data.getDataPtr(), shadow_data.getDataSize());
				cptr+= shadow_data.getDataSize();
				memcpy(cptr, &save_id, sizeof(int));
				cptr+=sizeof(int);
				size_t bsize=sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+ shadow_data.getDataSize() +sizeof(int);
				if(with_checksum)
				{
					sha256_update(&shactx, (unsigned char*)buffer, (unsigned int)bsize);
					sha256_final(&shactx, (unsigned char*)cptr);
					bsize+=c_hashsize;
					sha256_init(&shactx);
				}
				bool b=pipe->Write(buffer, bsize, initial_send_timeout);
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
				if (!sendBitmap(fs.get(), drivesize, blocksize))
				{
					run = false;
					break;
				}
			}
			
			clientSend=new ClientSend(pipe, blocksize+sizeof(int64), 2000);
			THREADPOOL_TICKET send_ticket=Server->getThreadPool()->execute(clientSend, "full image transfer");

			unsigned int needed_bufs=64;
			int64 last_hash_block=-1;
			std::vector<char*> bufs;
			int64 startpos = image_inf->startpos < 0 ? 0 : image_inf->startpos;
			for(int64 i=startpos,blocks=drivesize/blocksize;i<blocks;i+=64)
			{
				std::vector<char*> n_bufs= clientSend->getBuffers(needed_bufs);
				bufs.insert(bufs.end(), n_bufs.begin(), n_bufs.end() );
				needed_bufs=0;

				unsigned int n=(unsigned int)(std::min)(blocks-i, (int64)64);
				std::vector<int64> secs=fs->readBlocks(i, n, bufs, sizeof(int64));
				if(fs->hasError())
				{
					ImageErrRunning("Error while reading from shadow copy device (1). "+getFsErrMsg());
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
							clientSend->sendBuffer(bufs[idx], sizeof(int64)+blocksize, false);
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
								char* cb=clientSend->getBuffer();
								int64 bs=-126;
								int64 nextblock=j+1;
								memcpy(cb, &bs, sizeof(int64) );
								memcpy(cb+sizeof(int64), &nextblock, sizeof(int64));
								memcpy(cb+2*sizeof(int64), dig, c_hashsize);
								clientSend->sendBuffer(cb, 2*sizeof(int64)+c_hashsize, false);
								notify_cs=true;

								if (hdat_img.get() != NULL
									&& IndexThread::getShadowId(hdat_vol, hdat_img.get())==r_shadow_id)
								{
									hdat_img->Write(sizeof(int) + (j / vhdblocks)*c_hashsize, reinterpret_cast<char*>(dig), c_hashsize);
								}
								else
								{
									hdat_img.reset();
								}
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
						clientSend->sendBuffer(bufs[j], sizeof(int64)+blocksize, false);
						notify_cs=true;
					}
				}
				if(notify_cs)
				{
					clientSend->notifySendBuffer();
				}

				if(!secs.empty())
					bufs.erase(bufs.begin(), bufs.begin()+secs.size() );
				if(clientSend->hasError() )
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

				if(Server->getTimeMS() - last_shadowcopy_update > 1*60*60*1000)
				{
					updateShadowCopyStarttime(save_id);
					last_shadowcopy_update = Server->getTimeMS();
				}
			}

			for(size_t i=0;i<bufs.size();++i)
			{
				clientSend->freeBuffer(bufs[i]);
			}
			clientSend->doExit();
			Server->getThreadPool()->waitFor(send_ticket);
			if (clientSend->hasError())
			{
				Server->Log("Pipe broken -4", LL_ERROR);
				run = false;
			}
			delete clientSend;

			if(!run)break;

			char lastbuffer[sizeof(int64)];
			int64 lastn=-123;
			memcpy(lastbuffer,&lastn, sizeof(int64));
			bool b=pipe->Write(lastbuffer, sizeof(int64), running_send_timeout);
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

	bool success = !has_error;

	if (success && !image_inf->no_shadowcopy)
	{
		ClientConnector::updateLastBackup();
		IndexThread::execute_postbackup_hook("postimagebackup", 0, std::string());
	}

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
	return success;
}

void ImageThread::removeShadowCopyThread(int save_id)
{
	if(!image_inf->no_shadowcopy)
	{
		IPipe* local_pipe=Server->createMemoryPipe();

		CWData data;
		data.addChar(IndexThread::IndexThreadAction_ReleaseShadowcopy);
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

bool ImageThread::sendIncrImageThread(void)
{
	char *zeroblockbuf=NULL;
	std::vector<char*> blockbufs;

	bool has_error=true;
	bool with_checksum=image_inf->with_checksum;

	int save_id=-1;
	int update_cnt=0;

	lastsendtime=Server->getTimeMS();
	int64 last_shadowcopy_update = Server->getTimeMS();

	std::auto_ptr<IFile> hdat_img;
	std::string hdat_vol;
	int r_shadow_id = -1;
	str_map other_vols;

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
				if (image_inf->shadowdrive.find("|") != std::string::npos)
				{
					ParseParamStrHttp(getafter("|", image_inf->shadowdrive), &other_vols);
					image_inf->shadowdrive = getuntil("|", image_inf->shadowdrive);
				}
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
				if (image_inf->shadow_id == -1)
				{
					ImageErr("Creating shadow copy failed. See client log file for details.");
				}
				else
				{
					ImageErr("Shadow copy not found. May have been deleted.");
				}
				run=false;
			}
			mempipe->Write("exit");
			mempipe=NULL;

			if (run)
			{
				hdat_vol = image_inf->image_letter;
				hdat_img.reset(openHdatF(image_inf->image_letter, true));

				if (hdat_vol.size() == 1)
				{
					hdat_vol += ":";
				}

				if (hdat_img.get() != NULL)
				{
					hdat_img->Read(0, reinterpret_cast<char*>(&r_shadow_id), sizeof(r_shadow_id));

					if (r_shadow_id==-1
						|| (r_shadow_id != image_inf->shadow_id
						     && r_shadow_id != save_id) )
					{
						hdat_img.reset();
					}
				}
			}
		}
		else
		{
			std::auto_ptr<IFilesystem> fs;
			FsShutdownHelper shutdown_helper;
			if (!image_inf->shadowdrive.empty())
			{
				fs.reset(image_fak->createFilesystem(image_inf->shadowdrive, IFSImageFactory::EReadaheadMode_Overlapped,
					IndexThread::backgroundBackupsEnabled(std::string()), image_inf->image_letter, this));
				shutdown_helper.reset(fs.get());
			}
			if (fs.get() == NULL)
			{
				int tloglevel = LL_ERROR;
				if (image_inf->shadowdrive.empty())
				{
					tloglevel = LL_INFO;
				}
				ImageErr("Opening filesystem on device failed. Stopping.", tloglevel);
				Server->Log("Device file: \"" + image_inf->shadowdrive + "\"", LL_INFO);
				run = false;
				break;
			}

			int64 changed_blocks = 0;
			int64 unchanged_blocks = 0;

			std::auto_ptr<IReadOnlyBitmap> previous_bitmap;
			if (bitmapfile != NULL
				&& hdat_img.get() != NULL)
			{
				previous_bitmap.reset(image_fak->createClientBitmap(bitmapfile));
				if (previous_bitmap.get() == NULL
					|| previous_bitmap->hasError())
				{
					ImageErr("Error opening previous client bitmap", LL_ERROR);
					run = false;
					break;
				}
			}
			else if (hdat_img.get() != NULL)
			{
				Server->Log("Need previous client bitmap for image backup with CBT. Disabling CBT.", LL_ERROR);
				hdat_img.reset();
			}

			if (hdat_img.get() != NULL)
			{
				unsigned int blocksize = (unsigned int)fs->getBlocksize();
				int64 drivesize = fs->getSize();

				cbt_bitmap.resize(drivesize / c_vhdblocksize + (drivesize % c_vhdblocksize == 0 ? 0 : 1));

				std::vector<char> buf;
				buf.resize(4096);

				hdat_img->Seek(sizeof(int));
				_u32 read;
				int64 imgpos = 0;
				int64 vhdblockpos = 0;
				do
				{
					read = hdat_img->Read(buf.data(), static_cast<_u32>(buf.size()));

					for (_u32 i = 0; i < read; 
						i += SHA256_DIGEST_SIZE, ++vhdblockpos,
						imgpos += c_vhdblocksize)
					{
						bool fs_has_block = false;
						bool bitmap_diff = false;
						for (int64 j = imgpos; j < imgpos + c_vhdblocksize
							&& j<drivesize; j += blocksize)
						{
							if (fs->hasBlock(j / blocksize))
							{
								fs_has_block = true;
								if (!previous_bitmap->hasBlock(j / blocksize))
								{
									bitmap_diff = true;
									break;
								}
							}
						}

						if (imgpos<drivesize 
							&& fs_has_block)
						{
							if (bitmap_diff
								|| buf_is_zero(reinterpret_cast<unsigned char*>(&buf[i]), SHA256_DIGEST_SIZE))
							{
								++changed_blocks;
								cbt_bitmap.set(vhdblockpos, true);
							}
							else
							{
								bool has_hashdata = false;
								char hashdata_buf[c_hashsize];
								int64 hnum = imgpos / c_vhdblocksize;
								if (hashdatafile->Size() >= (hnum + 1)*c_hashsize)
								{
									if (hashdatafile->Read(hnum*c_hashsize, hashdata_buf, c_hashsize) != c_hashsize)
									{
										Server->Log("Reading hashdata failed!", LL_ERROR);
									}
									else
									{
										has_hashdata = true;
									}
								}

								if (has_hashdata
									&& memcmp(hashdata_buf, &buf[i], c_hashsize) == 0)
								{
									++unchanged_blocks;
								}
								else
								{
									++changed_blocks;
									cbt_bitmap.set(vhdblockpos, true);
								}
							}
						}
					}

				} while (read > 0);

				previous_bitmap.reset();

				while (imgpos < drivesize)
				{
					int64 vhdblock = imgpos / c_vhdblocksize;

					++changed_blocks;
					cbt_bitmap.set(vhdblock, true);

					imgpos += c_vhdblocksize;
				}
			}

			curr_fs = fs.get();

			sha256_ctx shactx;

			unsigned int blocksize=(unsigned int)fs->getBlocksize();
			int64 drivesize=fs->getSize();
			int64 blockcnt=fs->calculateUsedSpace()/blocksize;
			blocks_per_vhdblock =c_vhdblocksize/blocksize;
			int64 currvhdblock=0;
			int64 numblocks=drivesize/blocksize;
			CWData shadow_data;
			createShadowData(other_vols, shadow_data);

			if (hdat_img.get() != NULL
				&& unchanged_blocks>0)
			{
				blockcnt = (changed_blocks*c_vhdblocksize) / blocksize;
				blockcnt *= -1;
			}

			if(image_inf->startpos<0)
			{
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+ shadow_data.getDataSize() +sizeof(int)+c_hashsize];
				char *cptr=buffer;
				memcpy(cptr, &blocksize, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &drivesize, sizeof(int64) );
				cptr+=sizeof(int64);
				memcpy(cptr, &blockcnt, sizeof(int64) );
				cptr+=sizeof(int64);
				unsigned char* flags = reinterpret_cast<unsigned char*>(cptr);
				*flags = 0;
#ifndef VSS_XP //persistence
#ifndef VSS_S03
				*flags |= ImageFlag_Persistent;
#endif
#endif
				if (image_inf->no_shadowcopy)
				{
					*flags = *flags & (~ImageFlag_Persistent);
			}
				if (image_inf->with_bitmap)
				{
					*flags |= ImageFlag_Bitmap;
				}
				++cptr;
				unsigned int shadowdata_size=little_endian((unsigned int)shadow_data.getDataSize());
				memcpy(cptr, &shadowdata_size, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, shadow_data.getDataPtr(), shadow_data.getDataSize());
				cptr+= shadow_data.getDataSize();
				memcpy(cptr, &save_id, sizeof(int));
				cptr+=sizeof(int);
				size_t bsize=sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+ shadow_data.getDataSize() +sizeof(int);
				if(with_checksum)
				{
					sha256_init(&shactx);
					sha256_update(&shactx, (unsigned char*)buffer, (unsigned int)bsize);
					sha256_final(&shactx, (unsigned char*)cptr);
					bsize+=c_hashsize;
					sha256_init(&shactx);
				}
				bool b=pipe->Write(buffer, bsize, initial_send_timeout);
				delete []buffer;
				if(!b)
				{
					Server->Log("Pipe broken -1", LL_ERROR);
					run=false;
					break;
				}
			}

			if (image_inf->with_bitmap)
			{
				if (!sendBitmap(fs.get(), drivesize, blocksize))
				{
					run = false;
					break;
				}
			}
			
			blockbufs.clear();
			blockbufs.resize(blocks_per_vhdblock);
			delete []zeroblockbuf;
			zeroblockbuf=new char[blocksize];
			memset(zeroblockbuf, 0, blocksize);

			clientSend = new ClientSend(pipe, blocksize+sizeof(int64), 2000);
			THREADPOOL_TICKET send_ticket=Server->getThreadPool()->execute(clientSend, "incr image transfer");

			int64 startpos = image_inf->startpos < 0 ? 0 : image_inf->startpos;
			for(int64 i=startpos,blocks=drivesize/blocksize;i<blocks;i+= blocks_per_vhdblock)
			{
				++update_cnt;
				if(update_cnt>10
					&& blockcnt>=0)
				{
					ClientConnector::updateRunningPc(image_inf->running_process_id, (int)(((float)i / (float)blocks)*100.f + 0.5f));
					update_cnt=0;
				}
				currvhdblock=i/ blocks_per_vhdblock;

#ifdef _DEBUG
				for (size_t k = 0; k<blockbufs.size(); ++k)
				{
					assert(blockbufs[k] == NULL);
				}
#endif

				bool has_data = false;

				if (cbt_bitmap.empty())
				{
					for (int64 j = i; j < blocks && j < i + blocks_per_vhdblock; ++j)
					{
						if (fs->hasBlock(j))
						{
							has_data = true;
						}
					}
				}
				else
				{
					has_data = cbt_bitmap.get(currvhdblock);
				}

				if(has_data)
				{
					bool has_hashdata = false;
					char hashdata_buf[c_hashsize];
					if (hashdatafile->Size() >= (currvhdblock + 1)*c_hashsize)
					{
						hashdatafile->Seek(currvhdblock*c_hashsize);
						if (hashdatafile->Read(hashdata_buf, c_hashsize) != c_hashsize)
						{
							Server->Log("Reading hashdata failed!", LL_ERROR);
						}
						else
						{
							has_hashdata = true;
						}
					}

					bool mixed = false;
					unsigned char digest[SHA256_DIGEST_SIZE];
					if (has_hashdata || with_checksum)
					{
						sha256_init(&shactx);
						for (int64 j = i; j < blocks && j < i + blocks_per_vhdblock; ++j)
						{
							char* buf = fs->readBlock(j);
							if (buf != NULL)
							{
								sha256_update(&shactx, (unsigned char*)buf, blocksize);
								blockbufs[j - i] = buf;
							}
							else
							{
								if (fs->hasError())
								{
									break;
								}
								sha256_update(&shactx, (unsigned char*)zeroblockbuf, blocksize);
								mixed = true;
								blockbufs[j - i] = NULL;
							}
						}
						if (fs->hasError())
						{
							for (size_t i = 0; i < blockbufs.size(); ++i)
							{
								if (blockbufs[i] != NULL)
								{
									fs->releaseBuffer(blockbufs[i]);
									blockbufs[i] = NULL;
								}
							}
							std::string oserrmsg;
							ImageErrRunning("Error while reading from shadow copy device (2). "+getFsErrMsg());
							run = false;
							break;
						}

						sha256_final(&shactx, digest);

						if (hdat_img.get() != NULL)
						{
							if (IndexThread::getShadowId(hdat_vol, hdat_img.get()) != r_shadow_id)
							{
								hdat_img.reset();
							}
						}

						if (hdat_img.get() != NULL)
						{
							hdat_img->Write(sizeof(int) + (i / blocks_per_vhdblock)*c_hashsize, reinterpret_cast<char*>(digest), c_hashsize);
						}
					}

					if(!has_hashdata || memcmp(hashdata_buf, digest, c_hashsize) != 0)
					{
						Server->Log("Block did change: "+convert(i)+" mixed="+convert(mixed), LL_DEBUG);
						bool notify_cs=false;
						for(int64 j=i;j<blocks && j<i+ blocks_per_vhdblock;++j)
						{
							if(blockbufs[j-i]!=NULL)
							{
								char* cb=clientSend->getBuffer();
								memcpy(cb, &j, sizeof(int64) );
								memcpy(&cb[sizeof(int64)], blockbufs[j-i], blocksize);
								clientSend->sendBuffer(cb, sizeof(int64)+blocksize, false);
								notify_cs=true;
								lastsendtime=Server->getTimeMS();
								fs->releaseBuffer(blockbufs[j-i]);
								blockbufs[j-i]=NULL;
							}
						}

						if(notify_cs)
						{
							clientSend->notifySendBuffer();
							if(clientSend->hasError())
							{
								Server->Log("Pipe broken -2", LL_ERROR);
								run=false;
							}
						}

						if(with_checksum)
						{
							char* cb=clientSend->getBuffer();
							int64 bs=-126;
							int64 nextblock=(std::min)(blocks, i+ blocks_per_vhdblock);
							memcpy(cb, &bs, sizeof(int64) );
							memcpy(cb+sizeof(int64), &nextblock, sizeof(int64));
							memcpy(cb+2*sizeof(int64), digest, c_hashsize);
							clientSend->sendBuffer(cb, 2*sizeof(int64)+c_hashsize, true);
							//Server->Log("Block hash="+base64_encode(digest, c_hashsize), LL_DEBUG);
						}
					}
					else
					{
						//Server->Log("Block didn't change: "+convert(i), LL_DEBUG);
						int64 tt=Server->getTimeMS();
						if(tt-lastsendtime>10000)
						{
							int64 bs=-125;
							char* buffer=clientSend->getBuffer();
							memcpy(buffer, &bs, sizeof(int64) );
							clientSend->sendBuffer(buffer, sizeof(int64), true);

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
						char* buffer=clientSend->getBuffer();
						memcpy(buffer, &bs, sizeof(int64) );
						clientSend->sendBuffer(buffer, sizeof(int64), true);

						lastsendtime=tt;
					}
				}

				if(IdleCheckerThread::getPause())
				{
					Server->wait(30000);
				}

				if(Server->getTimeMS() - last_shadowcopy_update > 1*60*60*1000)
				{
					updateShadowCopyStarttime(save_id);
					last_shadowcopy_update = Server->getTimeMS();
				}
			}

			clientSend->doExit();
			Server->getThreadPool()->waitFor(send_ticket);
			if (clientSend->hasError())
			{
				Server->Log("Pipe broken -4", LL_ERROR);
				run = false;
			}
			delete clientSend;

			if(!run)break;

			char lastbuffer[sizeof(int64)];
			int64 lastn=-123;
			memcpy(lastbuffer,&lastn, sizeof(int64));
			bool b=pipe->Write(lastbuffer, sizeof(int64), running_send_timeout);
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

	std::string hashdatafile_fn=hashdatafile->getFilename();
	Server->destroy(hashdatafile);
	Server->deleteFile(hashdatafile_fn);

	if (bitmapfile != NULL)
	{
		ScopedDeleteFile del_bitmap_file(bitmapfile);
	}

	Server->Log("Sending image done", LL_INFO);

	bool success = !has_error;

	if (success && !image_inf->no_shadowcopy)
	{
		ClientConnector::updateLastBackup();
		IndexThread::execute_postbackup_hook("postimagebackup", 0, std::string());
	}

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

	return success;
}

void ImageThread::operator()(void)
{
	ScopedBackgroundPrio background_prio(false);
	if(IndexThread::backgroundBackupsEnabled(std::string()))
	{
		background_prio.enable();
	}

	bool success = false;

	if(image_inf->thread_action==TA_FULL_IMAGE)
	{
		success = sendFullImageThread();
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
			success = sendIncrImageThread();
		}
		else
		{
			Server->Log("Error receiving hashdata. timouts="+convert(timeouts)+" isquitting="+convert(client->isQuitting()), LL_ERROR);
			if(image_inf->shadowdrive.empty() && !image_inf->no_shadowcopy)
			{
				mempipe->Write("exit");
				mempipe=NULL;
			}
		}
	}
	ClientConnector::removeRunningProcess(image_inf->running_process_id, success);
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
		data.addString(image_inf->clientsubname);

		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}
}

bool ImageThread::sendBitmap(IFilesystem* fs, int64 drivesize, unsigned int blocksize)
{
	int64 totalblocks = drivesize / blocksize;
	if (drivesize%blocksize != 0)
	{
		++totalblocks;
	}

	size_t bitmap_size = totalblocks / 8;
	if (totalblocks % 8 != 0)
	{
		++bitmap_size;
	}

	const unsigned char* bitmap = fs->getBitmap();

	sha256_ctx shactx;
	sha256_init(&shactx);

	unsigned int endian_blocksize = little_endian(blocksize);
	if (!pipe->Write(reinterpret_cast<char*>(&endian_blocksize), sizeof(endian_blocksize), initial_send_timeout, false))
	{
		Server->Log("Pipe broken while sending bitmap blocksize", LL_ERROR);
		return false;
	}

	sha256_update(&shactx, reinterpret_cast<unsigned char*>(&endian_blocksize), sizeof(endian_blocksize));

	while (bitmap_size>0)
	{
		size_t tosend = (std::min)((size_t)4096, bitmap_size);

		sha256_update(&shactx, bitmap, (unsigned int)tosend);

		if (!pipe->Write(reinterpret_cast<const char*>(bitmap), tosend, initial_send_timeout, false))
		{
			Server->Log("Pipe broken while sending bitmap", LL_ERROR);
			return false;
		}

		bitmap_size -= tosend;
		bitmap += tosend;
	}

	unsigned char dig[c_hashsize];
	sha256_final(&shactx, dig);

	if (!pipe->Write(reinterpret_cast<char*>(dig), c_hashsize, initial_send_timeout))
	{
		Server->Log("Pipe broken while sending bitmap checksum", LL_ERROR);
		return false;
	}

	return true;
}

std::string ImageThread::getFsErrMsg()
{
	if (curr_fs->getOsErrorCode() == fs_error_read_timeout)
	{
		return " Timeout while reading block from disk";
	}
	else if (curr_fs->getOsErrorCode() != 0)
	{
		std::string extra_info;
		if (!image_inf->shadowdrive.empty())
		{
			std::auto_ptr<IFile> dev(Server->openFile(image_inf->shadowdrive, MODE_READ_DEVICE));
			if (dev.get() == NULL)
			{
				extra_info += ". Shadow copy " + image_inf->shadowdrive + " not present anymore. May have been removed";
			}
		}

		return " " + os_format_errcode(curr_fs->getOsErrorCode()) + extra_info;
	}
	else
	{
		return "";
	}
}

std::string ImageThread::hdatFn(std::string volume)
{
	if (!IndexThread::normalizeVolume(volume))
	{
		return std::string();
	}

	return volume + os_file_sep() + "System Volume Information\\urbhdat_img.dat";
}

IFsFile* ImageThread::openHdatF(std::string volume, bool share)
{
	if (!IndexThread::normalizeVolume(volume))
	{
		return NULL;
	}

#ifdef _WIN32

	DWORD share_mode = FILE_SHARE_READ;

	if (share)
	{
		share_mode |= FILE_SHARE_WRITE;
	}

	HANDLE hfile = CreateFileA((volume + os_file_sep() + "System Volume Information\\urbhdat_img.dat").c_str(), GENERIC_WRITE|GENERIC_READ, share_mode, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (hfile == INVALID_HANDLE_VALUE)
	{
		return NULL;
	}

	return Server->openFileFromHandle(hfile, volume + os_file_sep() + "System Volume Information\\urbhdat_img.dat");
#else
	return NULL;
#endif
}

int64 ImageThread::nextBlock(int64 curr_block)
{
	int64 size_blocks = curr_fs->getSize() / curr_fs->getBlocksize();

	while (curr_block + 1<size_blocks)
	{
		if (!cbt_bitmap.empty()
			&& (curr_block + 1) % blocks_per_vhdblock == 0)
		{
			int64 vhdblock = (curr_block + 1) / blocks_per_vhdblock;
			if (!cbt_bitmap.get(vhdblock))
			{
				curr_block += blocks_per_vhdblock;
				continue;
			}
		}

		++curr_block;

		if (curr_fs->hasBlock(curr_block))
		{
			return curr_block;
		}
	}

	return -1;
}

void ImageThread::slowReadWarning(int64 passed_time_ms, int64 curr_block)
{
	Server->Log("Waiting for block " + convert(curr_block) + 
		" since " + PrettyPrintTime(passed_time_ms), LL_WARNING);
}

void ImageThread::waitingForBlockCallback(int64 curr_block)
{
	int64 tt = Server->getTimeMS();
	if (tt - lastsendtime>10000)
	{
		int64 bs = -125;
		char* buffer = clientSend->getBuffer();
		memcpy(buffer, &bs, sizeof(int64));
		clientSend->sendBuffer(buffer, sizeof(int64), true);

		lastsendtime = tt;
	}
}
