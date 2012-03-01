#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"

#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IFilesystem.h"

#include "../urbackupcommon/fileclient/data.h"
#include "../urbackupcommon/sha2/sha2.h"

#include "ClientService.h"
#include "ImageThread.h"
#include "ClientSend.h"
#include "client.h"

extern IFSImageFactory *image_fak;

ImageThread::ImageThread(ClientConnector *client, IPipe *pipe, IPipe **mempipe, ImageInformation *image_inf, std::string server_token, IFile *hashdatafile)
	: client(client), pipe(pipe), mempipe(mempipe), image_inf(image_inf), server_token(server_token), hashdatafile(hashdatafile)
{
}

void ImageThread::ImageErr(const std::string &msg)
{
	Server->Log(msg, LL_ERROR);
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

void ImageThread::ImageErrRunning(const std::string &msg)
{
	Server->Log(msg, LL_ERROR);
	int64 bs=-124;
	char *buffer=new char[sizeof(int64)+msg.size()];
	memcpy(buffer, &bs, sizeof(int64) );
	memcpy(&buffer[sizeof(int64)], msg.c_str(), msg.size());
	pipe->Write(buffer, sizeof(int64)+msg.size());
	delete [] buffer;
}

void ImageThread::sendFullImageThread(void)
{
	bool has_error=true;

	int save_id=-1;

	bool run=true;
	while(run)
	{
		if(image_inf->shadowdrive.empty() && !image_inf->no_shadowcopy)
		{
			std::string msg;
			(*mempipe)->Read(&msg);
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
			(*mempipe)->Write("exit");
			*mempipe=Server->createMemoryPipe();
		}
		else
		{
			IFilesystem *fs=image_fak->createFilesystem(Server->ConvertToUnicode(image_inf->shadowdrive));
			if(fs==NULL)
			{
				ImageErr("Opening Shadow drive filesystem failed. Stopping.");
				run=false;
				break;
			}

			unsigned int blocksize=(unsigned int)fs->getBlocksize();
			int64 drivesize=fs->getSize();
			int64 blockcnt=fs->calculateUsedSpace()/blocksize;
			int64 ncurrblocks=0;

			if(image_inf->startpos==0)
			{
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int)];
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
				if(image_inf->no_shadowcopy)
				{
					*cptr=0;
				}
				++cptr;
				unsigned int shadowdrive_size=(unsigned int)image_inf->shadowdrive.size();
				memcpy(cptr, &shadowdrive_size, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &image_inf->shadowdrive[0], image_inf->shadowdrive.size());
				cptr+=image_inf->shadowdrive.size();
				memcpy(cptr, &save_id, sizeof(int));
				cptr+=sizeof(int);

				bool b=pipe->Write(buffer, sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int) );
				if(!b)
				{
					Server->Log("Pipe broken -1", LL_ERROR);
					run=false;
					break;
				}
			}
			
			ClientSend *cs=new ClientSend(pipe, blocksize+sizeof(int64), 2000);
			THREADPOOL_TICKET send_ticket=Server->getThreadPool()->execute(cs);

			unsigned int needed_bufs=64;
			std::vector<char*> bufs;
			for(int64 i=image_inf->startpos,blocks=drivesize/blocksize;i<blocks;i+=64)
			{
				std::vector<char*> n_bufs=cs->getBuffers(needed_bufs);
				bufs.insert(bufs.end(), n_bufs.begin(), n_bufs.end() );
				needed_bufs=0;

				unsigned int n=(unsigned int)(std::min)(blocks-i, (int64)64);
				std::vector<int64> secs=fs->readBlocks(i, n, bufs, sizeof(int64));
				needed_bufs+=(_u32)secs.size();
				for(size_t j=0;j<secs.size();++j)
				{
					memcpy(bufs[j], &secs[j], sizeof(int64) );
					cs->sendBuffer(bufs[j], sizeof(int64)+blocksize);
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
				
				if(IdleCheckerThread::getPause())
				{
					Server->wait(5000);
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
		std::string msg;
		(*mempipe)->Read(&msg);
		if(msg.find("done")!=0)
		{
			Server->Log("Removing shadow copy failed in image streaming: "+msg, LL_ERROR);
		}
		(*mempipe)->Write("exit");
		*mempipe=Server->createMemoryPipe();
	}
	client->doQuitClient();
}

void ImageThread::removeShadowCopyThread(int save_id)
{
	if(!image_inf->no_shadowcopy)
	{
		CWData data;
		data.addChar(3);
		data.addVoidPtr(*mempipe);
		data.addString(image_inf->image_letter);
		data.addString(server_token);
		data.addUChar(1);
		data.addInt(save_id);

		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}
}

const unsigned int c_vhdblocksize=(1024*1024/2);
const unsigned int c_hashsize=32;

void ImageThread::sendIncrImageThread(void)
{
	char *blockbuf=NULL;
	char *blockdatabuf=NULL;
	bool *has_blocks=NULL;
	char *zeroblockbuf=NULL;

	bool has_error=true;

	int save_id=-1;
	int update_cnt=0;

	unsigned int lastsendtime=Server->getTimeMS();

	bool run=true;
	while(run)
	{
		if(image_inf->shadowdrive.empty())
		{
			std::string msg;
			(*mempipe)->Read(&msg);
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
			(*mempipe)->Write("exit");
			*mempipe=Server->createMemoryPipe();
		}
		else
		{
			IFilesystem *fs=image_fak->createFilesystem(Server->ConvertToUnicode(image_inf->shadowdrive));
			if(fs==NULL)
			{
				ImageErr("Opening Shadow drive filesystem failed. Stopping.");
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
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int)];
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
				bool b=pipe->Write(buffer, sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+image_inf->shadowdrive.size()+sizeof(int) );
				if(!b)
				{
					Server->Log("Pipe broken -1", LL_ERROR);
					run=false;
					break;
				}
			}

			delete []blockbuf;
			blockbuf=new char[vhdblocks*blocksize];
			delete []has_blocks;
			has_blocks=new bool[vhdblocks];
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
					if( fs->readBlock(j, NULL) )
					{
						has_data=true;
						break;
					}
				}

				if(has_data)
				{
					sha256_init(&shactx);
					bool mixed=false;
					for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
					{
						char *dpos=blockbuf+(j-i)*blocksize;
						if( fs->readBlock(j, dpos) )
						{
							if(IdleCheckerThread::getPause())
							{
								Server->wait(5000);
							}
							sha256_update(&shactx, (unsigned char*)dpos, blocksize);
							has_blocks[j-i]=true;
						}
						else
						{
							sha256_update(&shactx, (unsigned char*)zeroblockbuf, blocksize);
							has_blocks[j-i]=false;
							mixed=true;
						}
					}
					unsigned char digest[c_hashsize];
					sha256_final(&shactx, digest);
					if(hashdatafile->Size()>=(currvhdblock+1)*c_hashsize)
					{
						hashdatafile->Seek(currvhdblock*c_hashsize);
						char hashdata_buf[c_hashsize];
						if( hashdatafile->Read(hashdata_buf, c_hashsize)!=c_hashsize )
						{
							Server->Log("Reading hashdata failed!", LL_ERROR);
						}
						if(memcmp(hashdata_buf, digest, c_hashsize)!=0)
						{
							//Server->Log("Block did change: "+nconvert(i)+" mixed="+nconvert(mixed), LL_DEBUG);
							for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
							{
								if(has_blocks[j-i])
								{
									char* cb=cs->getBuffer();
									memcpy(&cb[sizeof(int64)], blockbuf+(j-i)*blocksize, blocksize);
									memcpy(cb, &j, sizeof(int64) );
									cs->sendBuffer(cb, sizeof(int64)+blocksize);

									lastsendtime=Server->getTimeMS();

									if(cs->hasError())
									{
										Server->Log("Pipe broken -2", LL_ERROR);
										run=false;
										break;
									}
								}
							}							
						}
						else
						{
							//Server->Log("Block didn't change: "+nconvert(i), LL_DEBUG);
							unsigned int tt=Server->getTimeMS();
							if(tt-lastsendtime>10000)
							{
								int64 bs=-125;
								char* buffer=cs->getBuffer();
								memcpy(buffer, &bs, sizeof(int64) );
								cs->sendBuffer(buffer, sizeof(int64));

								lastsendtime=tt;
							}
						}
					}
					else
					{
						ImageErrRunning("Hashdata from server too small");
						run=false;
						break;
					}
					if(!run)break;
				}
				else
				{
					unsigned int tt=Server->getTimeMS();
					if(tt-lastsendtime>10000)
					{
						int64 bs=-125;
						char* buffer=cs->getBuffer();
						memcpy(buffer, &bs, sizeof(int64) );
						cs->sendBuffer(buffer, sizeof(int64));

						lastsendtime=tt;
					}
				}
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

	delete [] blockbuf;
	delete [] has_blocks;
	delete [] zeroblockbuf;

	Server->destroy(hashdatafile);

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
		std::string msg;
		(*mempipe)->Read(&msg);
		if(msg.find("done")!=0)
		{
			Server->Log("Removing shadow copy failed in image streaming: "+msg, LL_ERROR);
		}
		(*mempipe)->Write("exit");
		*mempipe=Server->createMemoryPipe();
	}
	client->doQuitClient();
}

void ImageThread::operator()(void)
{
#ifdef _WIN32
#ifdef THREAD_MODE_BACKGROUND_BEGIN
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
#endif
	if(image_inf->thread_action==TA_FULL_IMAGE)
	{
		sendFullImageThread();
	}
	else if(image_inf->thread_action==TA_INCR_IMAGE)
	{
		int timeouts=1800;
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
	}
	client->resetImageBackupStatus();
#ifdef _WIN32
#ifdef THREAD_MODE_BACKGROUND_END
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
#endif
#endif
}