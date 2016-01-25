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
#include "ImageBackup.h"
#include "../Interface/Server.h"
#include "mbr_code.h"
#include "server_log.h"
#include "server_update_stats.h"
#include "../stringtools.h"
#include "server_cleanup.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "server_writer.h"
#include "zero_hash.h"
#include "server_running.h"
#include "../md5.h"
#include "ClientMain.h"
#include <time.h>
#include <memory.h>
#include <stdlib.h>
#include "../Interface/Types.h"
#include <cstring>
#include "../urbackupcommon/sha2/sha2.h"
#include "../Interface/Pipe.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/mbrdata.h"
#include "server_ping.h"
#include "snapshot_helper.h"

const unsigned int status_update_intervall=1000;
const unsigned int eta_update_intervall=60000;
const unsigned int sector_size=512;
const unsigned int sha_size=32;
const size_t minfreespace_image=1000*1024*1024; //1000 MB
const unsigned int image_timeout=10*24*60*60*1000;
const unsigned int image_recv_timeout=30*60*1000;
const unsigned int image_recv_timeout_after_first=2*60*1000;
const unsigned int mbr_size=(1024*1024)/2;

extern std::string server_identity;
extern IFSImageFactory *image_fak;

namespace
{
	void writeZeroblockdata(void)
	{
		const int64 vhd_blocksize=(1024*1024/2);
		unsigned char *zeroes=new unsigned char[vhd_blocksize];
		memset(zeroes, 0, vhd_blocksize);
		unsigned char dig[sha_size];
		sha256(zeroes, vhd_blocksize, dig);
		IFile *out=Server->openFile("zero.hash", MODE_WRITE);
		out->Write((char*)dig, sha_size);
		Server->destroy(out);
		delete []zeroes;
	}

	enum ESendErr
	{
		ESendErr_Ok,
		ESendErr_Read,
		ESendErr_Send
	};

	ESendErr sendFileToPipe(IFile* file, IPipe* outputpipe, logid_t logid)
	{
		file->Seek(0);
		const unsigned int c_send_buffer_size=8192;
		char send_buffer[c_send_buffer_size];
		for(size_t i=0,hsize=(size_t)file->Size();i<hsize;i+=c_send_buffer_size)
		{
			size_t tsend=(std::min)((size_t)c_send_buffer_size, hsize-i);
			if(file->Read(send_buffer, (_u32)tsend)!=tsend)
			{
				ServerLogger::Log(logid, "Reading from file failed", LL_ERROR);
				return ESendErr_Read;
			}
			if(!outputpipe->Write(send_buffer, tsend))
			{
				ServerLogger::Log(logid, "Sending file data failed", LL_DEBUG);
				return ESendErr_Send;
			}
		}
		return ESendErr_Ok;
	}
}

ImageBackup::ImageBackup(ClientMain* client_main, int clientid, std::string clientname,
	std::string clientsubname, LogAction log_action, bool incremental, std::string letter, std::string server_token, std::string details)
	: Backup(client_main, clientid, clientname, clientsubname, log_action, false, incremental, server_token, details), pingthread_ticket(ILLEGAL_THREADPOOL_TICKET), letter(letter), synthetic_full(false)
{
}

bool ImageBackup::doBackup()
{
	bool cowraw_format = server_settings->getImageFileFormat()==image_file_format_cowraw;

	if(r_incremental)
	{
		ServerLogger::Log(logid, "Starting incremental image backup...", LL_INFO);
	}
	else
	{
		ServerLogger::Log(logid, "Starting full image backup...", LL_INFO);

		if(cowraw_format)
		{
			synthetic_full=true;
		}
		else if(client_main->isOnInternetConnection())
		{
			if(server_settings->getSettings()->internet_full_image_style==full_image_style_synthetic)
			{
				synthetic_full=true;
			}
		}
		else
		{
			if(server_settings->getSettings()->local_full_image_style==full_image_style_synthetic)
			{
				synthetic_full=true;
			}
		}

		if(letter=="ESP" || letter=="SYSVOL")
		{
			synthetic_full=false;
		}
	}

	bool image_hashed_transfer;
	if(client_main->isOnInternetConnection())
	{
		image_hashed_transfer= (server_settings->getSettings()->internet_image_transfer_mode=="hashed") && client_main->getProtocolVersions().image_protocol_version>0;
	}
	else
	{
		image_hashed_transfer= (server_settings->getSettings()->local_image_transfer_mode=="hashed") && client_main->getProtocolVersions().image_protocol_version>0;
	}

	int sysvol_id=-1;
	int esp_id=-1;
	if(strlower(letter)=="c:")
	{
		ServerLogger::Log(logid, "Backing up SYSVOL...", LL_DEBUG);
		client_main->stopBackupRunning(false);
		ImageBackup sysvol_backup(client_main, clientid, clientname, clientsubname, LogAction_NoLogging, false, "SYSVOL", server_token, "SYSVOL");
		sysvol_backup();

		if(sysvol_backup.getResult())
		{
			sysvol_id = sysvol_backup.getBackupId();
		}

		client_main->startBackupRunning(false);
		
		ServerLogger::Log(logid, "Backing up SYSVOL done.", LL_DEBUG);

		if(client_main->getProtocolVersions().efi_version>0)
		{
			ServerLogger::Log(logid, "Backing up EFI System Partition...", LL_DEBUG);
			client_main->stopBackupRunning(false);
			ImageBackup esp_backup(client_main, clientid, clientname, clientsubname, LogAction_NoLogging, false, "ESP", server_token, "ESP");
			esp_backup();

			if(esp_backup.getResult())
			{
				esp_id = esp_backup.getBackupId();
			}

			client_main->startBackupRunning(false);

			ServerLogger::Log(logid, "Backing up EFI System Partition done.", LL_DEBUG);
		}
	}

	pingthread = new ServerPingThread(client_main, clientname, status_id, client_main->getProtocolVersions().eta_version>0, server_token);
	pingthread_ticket=Server->getThreadPool()->execute(pingthread, "backup progress update");

	bool ret = false;
	std::string parent_image;
	if(r_incremental || synthetic_full)
	{
		bool incremental_to_last = synthetic_full || cowraw_format;

		if(!incremental_to_last)
		{
			if(client_main->isOnInternetConnection())
			{
				if(server_settings->getSettings()->internet_incr_image_style==incr_image_style_to_last)
				{
					incremental_to_last=true;
				}
			}
			else
			{
				if(server_settings->getSettings()->local_incr_image_style==incr_image_style_to_last)
				{
					incremental_to_last=true;
				}
			}
		}
		
		if(incremental_to_last)
		{
			ServerLogger::Log(logid, "Basing image backup on last incremental or full image backup", LL_INFO);
		}
		else
		{
			ServerLogger::Log(logid, "Basing image backup on last full image backup", LL_INFO);
		}
		
		SBackup last=getLastImage(letter, incremental_to_last);
		if(last.incremental==-2)
		{
			synthetic_full=false;
			ServerLogger::Log(logid, "Error retrieving last image backup. Doing full image backup instead.", LL_WARNING);
			ret = doImage(letter, "", 0, 0, image_hashed_transfer, server_settings->getImageFileFormat(),
					client_main->getProtocolVersions().client_bitmap_version>0);
		}
		else
		{
			ret = doImage(letter, last.path, last.incremental+1,
				cowraw_format?0:last.incremental_ref, image_hashed_transfer, server_settings->getImageFileFormat(),
				client_main->getProtocolVersions().client_bitmap_version>0);
		}
	}
	else
	{
		ret = doImage(letter, "", 0, 0, image_hashed_transfer, server_settings->getImageFileFormat(),
			      client_main->getProtocolVersions().client_bitmap_version>0);
	}

	if(ret)
	{
		if(sysvol_id!=-1)
		{
			backup_dao->saveImageAssociation(backupid, sysvol_id);
		}

		if(esp_id!=-1)
		{
			backup_dao->saveImageAssociation(backupid, esp_id);
		}
	}

	if(ret && letter!="SYSVOL" && letter!="ESP")
	{
		backup_dao->updateClientLastImageBackup(backupid, clientid);
	}

	if(pingthread!=NULL)
	{
		pingthread->setStop(true);
	}

	return ret;
}

namespace
{
	enum ETransferState
	{
		ETransferState_First,
		ETransferState_Bitmap,
		ETransferState_Image
	};
	
	const unsigned char ImageFlag_Persistent=1;
	const unsigned char ImageFlag_Bitmap=2;
}


bool ImageBackup::doImage(const std::string &pLetter, const std::string &pParentvhd, int incremental, int incremental_ref, bool transfer_checksum, std::string image_file_format, bool transfer_bitmap)
{
	CTCPStack tcpstack(client_main->isOnInternetConnection());
	IPipe *cc=client_main->getClientCommandConnection(10000);
	if(cc==NULL)
	{
		ServerLogger::Log(logid, "Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_ERROR);
		return false;
	}

	std::string sletter=pLetter;
	if(pLetter!="SYSVOL" && pLetter!="ESP")
	{
		sletter=pLetter[0];
	}

	bool with_checksum=false;
	std::string chksum_str="";
	if(transfer_checksum && client_main->getProtocolVersions().image_protocol_version>0)
	{
		chksum_str="&checksum=1";
		with_checksum=true;
	}
	
	if(transfer_bitmap)
	{
		chksum_str+="&bitmap=1";
	}

	if (!clientsubname.empty())
	{
		chksum_str += "&clientsubname=" + EscapeParamString(clientsubname);
	}

	chksum_str += "&status_id=" + convert(status_id);

	std::string identity= client_main->getSessionIdentity().empty()?server_identity:client_main->getSessionIdentity();

	if(pParentvhd.empty())
	{
		tcpstack.Send(cc, identity+"FULL IMAGE letter="+pLetter+"&token="+server_token+chksum_str);
	}
	else
	{
		IFile *hashfile=Server->openFile(os_file_prefix(pParentvhd+".hash"));
		if(hashfile==NULL)
		{
			ServerLogger::Log(logid, "Error opening hashfile", LL_ERROR);
			Server->Log("Starting image path repair...", LL_INFO);
			ServerUpdateStats::repairImages();
			Server->destroy(cc);
			return false;
		}
		std::string ts=identity+"INCR IMAGE letter="+pLetter+"&hashsize="+convert(hashfile->Size())+"&token="+server_token+chksum_str;
		size_t rc=tcpstack.Send(cc, ts);
		if(rc==0)
		{
			ServerLogger::Log(logid, "Sending 'INCR IMAGE' command failed", LL_ERROR);
			Server->destroy(cc);
			Server->destroy(hashfile);
			return false;
		}
		if(sendFileToPipe(hashfile, cc, logid)!=ESendErr_Ok)
		{
			ServerLogger::Log(logid, "Sending hashdata failed", LL_ERROR);
			Server->destroy(cc);
			Server->destroy(hashfile);
			return false;
		}
		Server->destroy(hashfile);
	}

	

	std::string imagefn;

	{
		std::string mbrd=getMBR(sletter);
		if(mbrd.empty())
		{
			if(pLetter!="SYSVOL" && pLetter!="ESP")
			{
				ServerLogger::Log(logid, "Error getting MBR data", LL_ERROR);
			}
		}
		else
		{
			imagefn = constructImagePath(sletter, image_file_format, pParentvhd);

			if (imagefn.empty())
			{
				Server->destroy(cc);
				return false;
			}

			IFile *mbr_file=Server->openFile(os_file_prefix(imagefn+".mbr"), MODE_WRITE);
			if(mbr_file!=NULL)
			{
				_u32 w=mbr_file->Write(mbrd);
				if(w!=mbrd.size())
				{
					Server->Log("Error writing mbr data.", LL_ERROR);
					Server->destroy(mbr_file);
					Server->destroy(cc);
					return false;
				}
				Server->destroy(mbr_file);
			}
			else
			{
				Server->Log("Error creating file for writing MBR data.", LL_ERROR);
				Server->destroy(cc);
				return false;
			}
		}
	}

	if (!imagefn.empty())
	{
		if (pParentvhd.empty())
		{
			backup_dao->newImageBackup(clientid, imagefn, 0, 0, client_main->getCurrImageVersion(), pLetter);
		}
		else
		{
			backup_dao->newImageBackup(clientid, imagefn, synthetic_full ? 0 : incremental, incremental_ref, client_main->getCurrImageVersion(), pLetter);
		}
	}

	backupid=static_cast<int>(db->getLastInsertID());

	std::string ret;
	int64 starttime=Server->getTimeMS();
	bool first=true;
	const unsigned int c_buffer_size=32768;
	char buffer[c_buffer_size];
	unsigned int blocksize=0xFFFFFFFF;
	unsigned int blockleft=0;
	int64 currblock=-1;
	char *blockdata=NULL;
	int64 drivesize;
	ServerVHDWriter *vhdfile=NULL;
	THREADPOOL_TICKET vhdfile_ticket;
	IVHDFile *r_vhdfile=NULL;
	IFile *hashfile=NULL;
	IFile *parenthashfile=NULL;
	std::auto_ptr<IFile> bitmap_file;
	int64 blockcnt=0;
	int64 numblocks=0;
	int64 blocks=0;
	int64 totalblocks=0;
	int64 mbr_offset=0;
	_u32 off=0;
	std::string shadowdrive;
	int shadow_id=-1;
	bool persistent=false;
	unsigned char *zeroblockdata=NULL;
	int64 nextblock=0;
	int64 last_verified_block=0;
	int64 vhd_blocksize=(1024*1024)/2;
	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, true);
	Server->getThreadPool()->execute(running_updater, "backup active update");
	unsigned char verify_checksum[sha_size];
	bool warned_about_parenthashfile_error=false;
	bool internet_connection = client_main->isOnInternetConnection();

	bool has_parent=false;
	if(!pParentvhd.empty())
		has_parent=true;

	sha256_ctx shactx;
	sha256_init(&shactx);

	_i64 transferred_bytes=0;
	_i64 transferred_bytes_real=0;
	int64 image_backup_starttime=Server->getTimeMS();

	unsigned int curr_image_recv_timeout=image_recv_timeout;

	int64 last_status_update=0;
	int64 last_eta_update=0;
	int64 last_eta_update_blocks=0;
	double eta_estimated_speed = 0;
	int64 eta_set_time = Server->getTimeMS();
	int64 speed_set_time = eta_set_time;
	int64 last_speed_received_bytes = 0;
	ServerStatus::setProcessEtaSetTime(clientname, status_id, eta_set_time);
	ETransferState transfer_state = ETransferState_First;
	unsigned char image_flags=0;
	size_t bitmap_read = 0;
	

	int num_hash_errors=0;

	while(Server->getTimeMS()-starttime<=image_timeout)
	{
		if(ServerStatus::getProcess(clientname, status_id).stop)
		{
			ServerLogger::Log(logid, "Server admin stopped backup.", LL_ERROR);
			goto do_image_cleanup;
		}
		size_t r=0;
		if(cc!=NULL)
		{
			r=cc->Read(&buffer[off], c_buffer_size-off, curr_image_recv_timeout);
		}
		if(r!=0)
			r+=off;
		starttime=Server->getTimeMS();
		off=0;
		if(r==0 )
		{
			ServerStatus::setProcessSpeed(clientname, status_id, 0);
			ServerStatus::setProcessEta(clientname, status_id, -1);
			if(persistent && nextblock!=0)
			{
				int64 continue_block=nextblock;
				if(continue_block%vhd_blocksize!=0 )
				{
					continue_block=(continue_block/vhd_blocksize)*vhd_blocksize;
				}
				bool reconnected=false;
				while(Server->getTimeMS()-starttime<=image_timeout)
				{
					if(ServerStatus::getProcess(clientname, status_id).stop)
					{
						ServerLogger::Log(logid, "Server admin stopped backup. (2)", LL_ERROR);
						goto do_image_cleanup;
					}
					ServerStatus::setROnline(clientname, false);
					if(cc!=NULL)
					{
						transferred_bytes+=cc->getTransferedBytes();
						transferred_bytes_real+=cc->getRealTransferredBytes();
						Server->destroy(cc);
						cc = NULL;
					}

					if (!internet_connection && client_main->isOnInternetConnection())
					{
						int icount = 0;
						while (icount < 4)
						{
							Server->wait(60000);
							if (client_main->isOnInternetConnection())
							{
								++icount;
							}
							else
							{
								break;
							}
						}

						if(icount>=4)
						{
							ServerLogger::Log(logid, "Stopped image backup because client is connected via Internet now", LL_WARNING);
							goto do_image_cleanup;
						}						
					}

					Server->Log("Trying to reconnect in doImage", LL_DEBUG);
					cc=client_main->getClientCommandConnection(10000);
					if(cc==NULL)
					{
						Server->wait(60000);
					}
					else
					{
						identity = client_main->getSessionIdentity().empty() ? server_identity : client_main->getSessionIdentity();
						reconnected=true;
						ServerStatus::setROnline(clientname, true);
						Server->Log("Reconnected.", LL_DEBUG);
						break;
					}
				}

				if(!reconnected)
				{
					ServerLogger::Log(logid, "Timeout while trying to reconnect", LL_ERROR);
					goto do_image_cleanup;
				}

				if(pParentvhd.empty())
				{
					std::string cmd = identity+"FULL IMAGE letter="+pLetter+"&shadowdrive="+shadowdrive+"&start="+convert(continue_block)+"&shadowid="+convert(shadow_id)+ "&status_id=" + convert(status_id);
					if(transfer_bitmap)
					{
						cmd+="&bitmap=1";
					}
					if(with_checksum)
					{
						cmd+="&checksum=1";
					}
					if (!clientsubname.empty())
					{
						cmd += "&clientsubname=" + EscapeParamString(clientsubname);
					}
					size_t sent = tcpstack.Send(cc, cmd);
					if(sent==0)
					{
						ServerLogger::Log(logid, "Sending 'FULL IMAGE' command failed", LL_WARNING);
						transferred_bytes+=cc->getTransferedBytes();
						transferred_bytes_real+=cc->getRealTransferredBytes();
						Server->destroy(cc);
						cc=NULL;
						continue;
					}
				}
				else
				{
					std::string ts = "INCR IMAGE letter=C:&shadowdrive=" + shadowdrive + "&start=" + convert(continue_block) + "&shadowid=" + convert(shadow_id) + "&hashsize=" + convert(parenthashfile->Size()) + "&status_id=" + convert(status_id);
					if(transfer_bitmap)
					{
						ts+="&bitmap=1";
					}
					if(with_checksum)
					{
						ts+="&checksum=1";
					}
					if (!clientsubname.empty())
					{
						ts += "&clientsubname=" + EscapeParamString(clientsubname);
					}
					size_t sent=tcpstack.Send(cc, identity+ts);
					if(sent==0)
					{
						ServerLogger::Log(logid, "Sending 'INCR IMAGE' command failed", LL_DEBUG);
						transferred_bytes+=cc->getTransferedBytes();
						transferred_bytes_real+=cc->getRealTransferredBytes();
						Server->destroy(cc);
						cc=NULL;
						continue;
					}
					ESendErr rc = sendFileToPipe(parenthashfile, cc, logid);
					if(rc==ESendErr_Send)
					{
						ServerLogger::Log(logid, "Sending hashdata failed", LL_DEBUG);
						transferred_bytes+=cc->getTransferedBytes();
						transferred_bytes_real+=cc->getRealTransferredBytes();
						Server->destroy(cc);
						cc=NULL;
						continue;
					}
					else if(rc!=ESendErr_Ok)
					{
						ServerLogger::Log(logid, "Reading from hashfile failed", LL_ERROR);
						goto do_image_cleanup;
					}
				}
				off=0;
				starttime=Server->getTimeMS();

				blockleft=0;
				currblock=-1;
			}
			else
			{
				ServerLogger::Log(logid, "Pipe to client unexpectedly closed has_error="+(cc==NULL?"NULL":convert(cc->hasError())), LL_ERROR);
				goto do_image_cleanup;
			}
		}
		else
		{
			if(transfer_state==ETransferState_First)
			{
				transfer_state = ETransferState_Image;
				size_t csize=sizeof(unsigned int);
				_u32 off_start=off;
				if(r>=csize)
				{
					memcpy(&blocksize, buffer, sizeof(unsigned int) );
					blocksize = little_endian(blocksize);
					off+=sizeof(unsigned int);
					vhd_blocksize/=blocksize;
				}
				if(blocksize==0xFFFFFFFF)
				{
					off+=sizeof(unsigned int);
					if(r>sizeof(uint64))
					{
						std::string err;
						err.resize(r-sizeof(uint64) );
						memcpy(&err[0], &buffer[off], r-off);
						if(pLetter!="SYSVOL" && pLetter!="ESP")
						{
							ServerLogger::Log(logid, "Request of image backup failed. Reason: "+err, LL_ERROR);
						}
						else
						{
							int loglevel = LL_WARNING;

							if (err == "Not found")
							{
								loglevel = LL_DEBUG;
							}

							if(pLetter=="SYSVOL")
								ServerLogger::Log(logid, "Request of SYSVOL failed. Reason: "+err+". This probably just means the Computer does not have a \"System restore\" volume which UrBackup can backup.", loglevel);
							else
								ServerLogger::Log(logid, "Request of EFI System Partition failed. Reason: "+err+". This probably just means the Computer does not have a EFI System Partition which UrBackup can backup.", loglevel);
						}
					}
					else
					{
						ServerLogger::Log(logid, "Error on client. No reason given.", LL_ERROR);
					}
					goto do_image_cleanup;
				}
				bool issmall=false;
				csize+=sizeof(int64);
				if(r>=csize)
				{
					memcpy(&drivesize, &buffer[off], sizeof(int64) );
					drivesize=little_endian(drivesize);
					off+=sizeof(int64);

					blockcnt=drivesize/blocksize;
					blocks=blockcnt;
					totalblocks=blockcnt;

					if(drivesize%blocksize!=0)
						++totalblocks;

					zeroblockdata=new unsigned char[blocksize];
					memset(zeroblockdata, 0, blocksize);

					if (imagefn.empty())
					{
						imagefn = constructImagePath(sletter, image_file_format, pParentvhd);

						if (imagefn.empty())
						{
							goto do_image_cleanup;
						}

						if (pParentvhd.empty())
						{
							backup_dao->newImageBackup(clientid, imagefn, 0, 0, client_main->getCurrImageVersion(), pLetter);
						}
						else
						{
							backup_dao->newImageBackup(clientid, imagefn, synthetic_full ? 0 : incremental, incremental_ref, client_main->getCurrImageVersion(), pLetter);
						}
					}

					IFSImageFactory::ImageFormat image_format;

					if(image_file_format==image_file_format_vhd)
					{
						image_format = IFSImageFactory::ImageFormat_VHD;
					}
					else if(image_file_format == image_file_format_cowraw)
					{
						image_format = IFSImageFactory::ImageFormat_RawCowFile;
					}
					else //default
					{
						image_format = IFSImageFactory::ImageFormat_CompressedVHD;
					}

					if(!has_parent)
					{
						r_vhdfile=image_fak->createVHDFile(os_file_prefix(imagefn), false, drivesize+(int64)mbr_size,
							(unsigned int)vhd_blocksize*blocksize, true,
							image_format);
					}
					else
					{
						r_vhdfile=image_fak->createVHDFile(os_file_prefix(imagefn), pParentvhd, false,
							true, image_format);
					}

					if(r_vhdfile==NULL || !r_vhdfile->isOpen())
					{
						ServerLogger::Log(logid, "Error opening VHD file \""+imagefn+"\"", LL_ERROR);
						goto do_image_cleanup;
					}

					hashfile=Server->openFile(os_file_prefix(imagefn+".hash"), MODE_WRITE);
					if(hashfile==NULL)
					{
						ServerLogger::Log(logid, "Error opening Hashfile \""+imagefn+".hash\"", LL_ERROR);
						goto do_image_cleanup;
					}
					
					if(transfer_bitmap)
					{
						bitmap_file.reset(Server->openFile(os_file_prefix(imagefn+".cbitmap"), MODE_RW_CREATE));
						if(bitmap_file.get()==NULL)
						{
							ServerLogger::Log(logid, "Error opening bitmap file \""+imagefn+".cbitmap\"", LL_ERROR);
							goto do_image_cleanup;
						}
						else
						{
							const char bitmap_magic[9] = "UrBBMM8C";
							bitmap_file->Write(bitmap_magic, 8);
						}
					}

					vhdfile=new ServerVHDWriter(r_vhdfile, blocksize, 5000, clientid, server_settings->getSettings()->use_tmpfiles_images, mbr_offset, hashfile, vhd_blocksize*blocksize, logid);
					vhdfile_ticket=Server->getThreadPool()->execute(vhdfile, "image backup writer");

					blockdata=vhdfile->getBuffer();

					if(has_parent)
					{
						parenthashfile=Server->openFile(os_file_prefix(pParentvhd+".hash"), MODE_READ);
						if(parenthashfile==NULL)
						{
							ServerLogger::Log(logid, "Error opening Parenthashfile \""+pParentvhd+".hash\"", LL_ERROR);
							goto do_image_cleanup;
						}
						ServerStatus::setProcessTotalBytes(clientname, status_id, totalblocks*blocksize);
					}
					else
					{
						ServerStatus::setProcessTotalBytes(clientname, status_id, blockcnt*blocksize);
					}

					mbr_offset=writeMBR(vhdfile, drivesize);
					if( mbr_offset==0 )
					{
						ServerLogger::Log(logid, "Error writing image MBR", LL_ERROR);
						goto do_image_cleanup;
					}
					else
					{
						vhdfile->setMbrOffset(mbr_offset);
					}
				}
				else
				{
					issmall=true;
				}
				csize+=+sizeof(int64);
				if(r>=csize)
				{
					memcpy(&blockcnt, &buffer[off], sizeof(int64) );
					blockcnt=little_endian(blockcnt);
					off+=sizeof(int64);
				}
				else
				{
					issmall=true;
				}
				csize+=1;
				if(r>=csize)
				{
					image_flags=static_cast<unsigned char>(buffer[off]);
					if(image_flags & ImageFlag_Persistent)
						persistent=true;
					if(image_flags & ImageFlag_Bitmap)
						transfer_state = ETransferState_Bitmap;
					++off;
				}
				else
				{
					issmall=true;
				}

				unsigned int shadowdrive_size=0;
				csize+=sizeof(unsigned int);
				if(r>=csize)
				{
					memcpy(&shadowdrive_size, &buffer[off], sizeof(unsigned int));
					shadowdrive_size = little_endian(shadowdrive_size);
					off+=sizeof(unsigned int);
					if(shadowdrive_size>0)
					{
						csize+=shadowdrive_size;
						if( r>=csize)
						{
							shadowdrive.resize(shadowdrive_size);
							memcpy(&shadowdrive[0],  &buffer[off], shadowdrive_size);
							off+=shadowdrive_size;
						}
						else
						{
							issmall=true;
						}
					}
				}
				else
				{
					issmall=true;
				}

				csize+=sizeof(int);
				if(r>=csize)
				{
					memcpy(&shadow_id, &buffer[off], sizeof(int));
					shadow_id = little_endian(shadow_id);
					off+=sizeof(int);
				}
				else
				{
					issmall=true;
				}

				if(with_checksum)
				{
					csize+=sha_size;
					if(r>=csize)
					{
						sha256_init(&shactx);
						sha256_update(&shactx, (unsigned char*)&buffer[off_start], (unsigned int)csize-sha_size);
						unsigned char dig[sha_size];
						sha256_final(&shactx, dig);
						if(memcmp(dig, &buffer[off], sha_size)!=0)
						{
							ServerLogger::Log(logid, "Checksum for first packet wrong. Stopping image backup.", LL_ERROR);
							goto do_image_cleanup;
						}
						off+=sha_size;
						sha256_init(&shactx);
					}
					else
					{
						issmall=true;
					}
				}

				if(issmall)
				{
					ServerLogger::Log(logid, "First packet too small", LL_WARNING);
					off = off_start + static_cast<_u32>(r);
					continue;
				}

				curr_image_recv_timeout=image_recv_timeout_after_first;

				if(r==off)
				{
					off=0;
					continue;
				}
			}
			if(transfer_state==ETransferState_Bitmap)
			{
				size_t bitmap_size = static_cast<size_t>(totalblocks/8);
				if(totalblocks%8!=0)
				{
					++bitmap_size;
				}

				bitmap_size += sizeof(unsigned int); //bitmap blocksize
				
				if(bitmap_size>1024*1024*1024)
				{
					ServerLogger::Log(logid, "Bitmap too large", LL_ERROR);
					goto do_image_cleanup;
				}
				
				if(bitmap_read<bitmap_size)
				{
					size_t toread = (std::min)(bitmap_size - bitmap_read, r-off);
					sha256_update(&shactx, (unsigned char*)&buffer[off], (unsigned int)toread);
				}
				
				bitmap_size += sha_size;
				
				size_t toread = (std::min)(bitmap_size - bitmap_read, r-off);
				
				if(bitmap_file.get()!=NULL 
					&& bitmap_file->Write(&buffer[off], static_cast<_u32>(toread))!=toread)
				{
					ServerLogger::Log(logid, "Error writing to bitmap file", LL_ERROR);
					goto do_image_cleanup;
				}
				
				bitmap_read+=toread;
				
				if(bitmap_read==bitmap_size)
				{
					off+=static_cast<_u32>(toread);
					
					if (bitmap_file.get() != NULL)
					{
						bitmap_file->Seek(8 + bitmap_size - sha_size);
						char dig_recv[sha_size];
						bitmap_file->Read(dig_recv, sha_size);

						unsigned char dig[sha_size];
						sha256_final(&shactx, dig);

						if (memcmp(dig, dig_recv, sha_size) != 0)
						{
							ServerLogger::Log(logid, "Checksum for bitmap wrong. Stopping image backup.", LL_ERROR);
							goto do_image_cleanup;
						}
					}
					
					sha256_init(&shactx);
					transfer_state = ETransferState_Image;
					transfer_bitmap = false;
					bitmap_file.reset();
				}
				else
				{
				    assert(r==off+toread);
				    off=0;
				    continue;
				}
			}
			while(true)
			{
				if(blockleft==0)
				{
					if(currblock!=-1) // write current block
					{
						if(nextblock<=currblock)
						{
							++numblocks;
							int64 ctime=Server->getTimeMS();
							if(ctime-last_status_update>status_update_intervall)
							{
								last_status_update = ctime;
								if(blockcnt!=0)
								{
									if(has_parent)
									{
										ServerStatus::setProcessDoneBytes(clientname, status_id, currblock*blocksize);
										ServerStatus::setProcessPcDone(clientname, status_id, 
											(int)(((double)currblock/(double)totalblocks)*100.0+0.5) );
									}
									else
									{
										ServerStatus::setProcessDoneBytes(clientname, status_id, numblocks*blocksize);
										ServerStatus::setProcessPcDone(clientname, status_id,
											(int)(((double)numblocks/(double)blockcnt)*100.0+0.5) );
									}
								}
							}

							if(ctime- last_eta_update>eta_update_intervall)
							{
								last_eta_update = ctime;

								int64 rel_blocks = has_parent ? currblock : numblocks;
								if (rel_blocks > 1000)
								{									
									int64 new_blocks = rel_blocks - last_eta_update_blocks;
									int64 passed_time = ctime - eta_set_time;

									if(new_blocks>0 && passed_time>0)
									{
										last_eta_update_blocks = rel_blocks;										
										
										eta_set_time = Server->getTimeMS();

										double speed_bpms = static_cast<double>(new_blocks) / passed_time;

										bool set_eta = false;
										if(eta_estimated_speed==0)
										{
											eta_estimated_speed = speed_bpms;
										}
										else if (eta_set_time - image_backup_starttime < 5 * 60 * 1000)
										{
											eta_estimated_speed = 0.9*eta_estimated_speed + 0.1*speed_bpms;
										}
										else
										{
											eta_estimated_speed = 0.99*eta_estimated_speed + 0.01*speed_bpms;
											set_eta = true;
										}

										if (set_eta)
										{
											int64 remaining_blocks = (has_parent ? totalblocks : blockcnt) - rel_blocks;
											ServerStatus::setProcessEta(clientname, status_id,
												static_cast<int64>(remaining_blocks / speed_bpms + 0.5), eta_set_time);
										}
									}									
								}
							}

							if (ctime - speed_set_time > 10000)
							{
								transferred_bytes += cc->getTransferedBytes();
								cc->resetTransferedBytes();

								int64 new_bytes = transferred_bytes - last_speed_received_bytes;
								int64 passed_time = ctime - speed_set_time;

								if (passed_time > 0)
								{
									speed_set_time = ctime;

									double speed_bpms = static_cast<double>(new_bytes) / passed_time;

									if (last_speed_received_bytes > 0)
									{
										ServerStatus::setProcessSpeed(clientname, status_id,
											speed_bpms);
									}

									last_speed_received_bytes = transferred_bytes;
								}
							}

							nextblock=updateNextblock(nextblock, currblock, &shactx, zeroblockdata,
								has_parent, vhdfile, hashfile, parenthashfile,
								blocksize, mbr_offset, vhd_blocksize, warned_about_parenthashfile_error, -1);

							sha256_update(&shactx, (unsigned char *)blockdata, blocksize);

							vhdfile->writeBuffer(mbr_offset+currblock*blocksize, blockdata, blocksize);
							blockdata=vhdfile->getBuffer();

							if(nextblock%vhd_blocksize==0 && nextblock!=0)
							{
								//Server->Log("Hash written "+convert(currblock), LL_DEBUG);
								sha256_final(&shactx, verify_checksum);
								hashfile->Write((char*)verify_checksum, sha_size);
								sha256_init(&shactx);
							}

							if(vhdfile->hasError())
							{
								ServerLogger::Log(logid, "FATAL ERROR: Could not write to VHD-File", LL_ERROR);
								ClientMain::sendMailToAdmins("Fatal error occured during image backup", ServerLogger::getWarningLevelTextLogdata(logid));
								goto do_image_cleanup;
							}
						}
						else if(nextblock-currblock>vhd_blocksize)
						{
							ServerLogger::Log(logid, "Block sent out of sequence. Expected block >="+convert(nextblock-vhd_blocksize-1)+" got "+convert(currblock)+". Stopping image backup.", LL_ERROR);
							goto do_image_cleanup;
						}

						currblock=-1;
					}
					bool accum=false;
					if(r-off>=sizeof(int64) )
					{
						memcpy(&currblock, &buffer[off], sizeof(int64) );
						currblock = little_endian(currblock);
						if(currblock==-123)
						{
							if(nextblock<=totalblocks)
							{
								nextblock=updateNextblock(nextblock, totalblocks, &shactx, zeroblockdata, has_parent, vhdfile,
									hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize, warned_about_parenthashfile_error, -1);

								if(nextblock!=0)
								{
									//Server->Log("Hash written "+convert(nextblock), LL_INFO);
									unsigned char dig[sha_size];
									sha256_final(&shactx, dig);
									hashfile->Write((char*)dig, sha_size);
								}
							}

							if(cc!=NULL)
							{
								transferred_bytes+=cc->getTransferedBytes();
								transferred_bytes_real+=cc->getRealTransferredBytes();
								Server->destroy(cc);
							}
							

							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
							}
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);

							bool vhdfile_err=false;

							if(vhdfile!=NULL)
							{
								if(!pParentvhd.empty() &&
									image_file_format == image_file_format_cowraw)
								{
									vhdfile->setDoTrim(true);
								}

								if(synthetic_full && image_file_format!=image_file_format_cowraw)
								{
									vhdfile->setDoMakeFull(true);
								}

								vhdfile->doExit();
								Server->getThreadPool()->waitFor(vhdfile_ticket);
								vhdfile_err=vhdfile->hasError();
								delete vhdfile;
								vhdfile=NULL;
							}

							if(hashfile!=NULL) Server->destroy(hashfile);

							IFile *t_file=Server->openFile(os_file_prefix(imagefn), MODE_READ);
							if(t_file!=NULL)
							{
								int64 image_size = t_file->RealSize();
								db->BeginWriteTransaction();
								backup_dao->setImageSize(image_size, backupid);
								backup_dao->addImageSizeToClient(clientid, image_size);
								if(vhdfile_err==false)
								{
									backup_dao->setImageBackupComplete(backupid);
								}
								db->EndTransaction();
								Server->destroy(t_file);
							}

							running_updater->stop();
							backup_dao->updateImageBackupRunning(backupid);

							int64 passed_time=Server->getTimeMS()-image_backup_starttime;
							if(passed_time==0) passed_time=1;

							ServerLogger::Log(logid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time)) ), LL_INFO );
							if(transferred_bytes_real>0)
							{
								ServerLogger::Log(logid, "(Before compression: "+PrettyPrintBytes(transferred_bytes_real)+" ratio: "+convert((float)transferred_bytes_real/transferred_bytes)+")");
							}

							return !vhdfile_err;
						}
						else if(currblock==-124 ||
#ifndef _WIN32 
							currblock==0xFFFFFFFFFFFFFFFFLLU)
#else
							currblock==0xFFFFFFFFFFFFFFFF)
#endif
						{
							if(r-off>sizeof(int64))
							{
								std::string err;
								err.resize(r-off-sizeof(int64) );
								memcpy(&err[0], &buffer[off+sizeof(int64)], r-off-sizeof(int64));

								if(err.find("|#|")!=std::string::npos)
								{
									err=getuntil("|#|", err);
								}

								ServerLogger::Log(logid, "Error on client occured: "+err, LL_ERROR);
							}
							Server->destroy(cc);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
								vhdfile->doExitNow();
								std::vector<THREADPOOL_TICKET> wf;wf.push_back(vhdfile_ticket);
								Server->getThreadPool()->waitFor(wf);
								delete vhdfile;
								vhdfile=NULL;
							}
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);
							running_updater->stop();
							delete []zeroblockdata;
							return false;
						}
						else if(currblock==-125) //ping
						{
							off+=sizeof(int64);
							currblock=-1;
						}
						else if(currblock==-126) //checksum
						{
							if(r-off>=2*sizeof(int64)+sha_size)
							{
								int64 hblock;
								unsigned char dig[sha_size];
								memcpy(&hblock, &buffer[off+sizeof(int64)], sizeof(int64));
								hblock = little_endian(hblock);
								memcpy(&dig, &buffer[off+2*sizeof(int64)], sha_size);


								if( (nextblock<hblock || (hblock==blocks && nextblock%vhd_blocksize!=0) ) && hblock>0)
								{
									if(nextblock<hblock)
									{
										nextblock=updateNextblock(nextblock, hblock-1, &shactx, zeroblockdata, has_parent,
											vhdfile, hashfile, parenthashfile, blocksize, mbr_offset,
											vhd_blocksize, warned_about_parenthashfile_error, -1);
										sha256_update(&shactx, (unsigned char *)zeroblockdata, blocksize);						
									}
									if( (nextblock%vhd_blocksize==0 || hblock==blocks) && nextblock!=0)
									{
										sha256_final(&shactx, verify_checksum);
										hashfile->Write((char*)verify_checksum, sha_size);
										sha256_init(&shactx);
									}
								}

								if( memcmp(verify_checksum, dig, sha_size)!=0)
								{
									Server->Log("Client hash="+base64_encode(dig, sha_size)+" Server hash="+base64_encode(verify_checksum, sha_size)+" hblock="+convert(hblock), LL_DEBUG);
									if(num_hash_errors<10)
									{
										ServerLogger::Log(logid, "Checksum for image block wrong. Retrying...", LL_WARNING);
										transferred_bytes+=cc->getTransferedBytes();
										Server->destroy(cc);
										cc=NULL;
										nextblock=last_verified_block;
										++num_hash_errors;
										break;
									}
									else
									{
										ServerLogger::Log(logid, "Checksum for image block wrong. Stopping image backup.", LL_ERROR);
										goto do_image_cleanup;
									}
								}
								else
								{
									if(hblock>=vhd_blocksize)
									{
										last_verified_block=hblock-vhd_blocksize;
									}
									else
									{
										last_verified_block=hblock;
									}
								}

								off+=2*sizeof(int64)+sha_size;								
							}
							else
							{
								accum=true;
							}
							currblock=-1;
						}
						else if(currblock==-127) //Empty VHD block
						{
							if(r-off>=2*sizeof(int64))
							{
								int64 vhdblock;
								memcpy(&vhdblock, &buffer[off+sizeof(int64)], sizeof(int64));
								vhdblock = little_endian(vhdblock);
								nextblock = updateNextblock(nextblock, vhdblock+vhd_blocksize, &shactx, zeroblockdata, has_parent, vhdfile,
									hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize, warned_about_parenthashfile_error, vhdblock);
							}
							else
							{
								accum=true;
							}
							currblock=-1;
						}
						else if(currblock<0)
						{
							ServerLogger::Log(logid, "Received unknown block number: "+convert(currblock)+". Stopping image backup.", LL_ERROR);
							goto do_image_cleanup;
						}
						else
						{
							off+=sizeof(int64);
							blockleft=blocksize;
						}
					}
					else
					{
						accum=true;
					}

					if(accum)
					{
						if(r-off>0)
						{
							memmove(buffer, &buffer[off], r-off);
							off=(_u32)r-off;
							break;
						}
						else
						{
							off=0;
							break;
						}
					}
				}
				else
				{
					unsigned int available=(std::min)(blockleft, (unsigned int)r-off);
					memcpy(&blockdata[blocksize-blockleft], &buffer[off], available);
					blockleft-=available;
					off+=available;
					if( off>=r )
					{
						off=0;
						break;
					}
				}
			}
		}
	}
	ServerLogger::Log(logid, "Timeout while transfering image data", LL_ERROR);

do_image_cleanup:

	ServerStatus::setProcessSpeed(clientname, status_id, 0);

	if(cc!=NULL)
	{
		transferred_bytes+=cc->getTransferedBytes();
	}
	int64 passed_time=Server->getTimeMS()-image_backup_starttime;
	if(passed_time==0) passed_time=1;
	ServerLogger::Log(logid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time) )), LL_INFO );
	if(transferred_bytes_real>0)
	{
		ServerLogger::Log(logid, "(Before compression: "+PrettyPrintBytes(transferred_bytes_real)+" ratio: "+convert((float)transferred_bytes_real/transferred_bytes)+")");
	}
	if(cc!=NULL)
		Server->destroy(cc);

	running_updater->stop();

	if(vhdfile!=NULL)
	{
		if(blockdata!=NULL)
			vhdfile->freeBuffer(blockdata);

		vhdfile->doExitNow();
		Server->getThreadPool()->waitFor(vhdfile_ticket);
		delete vhdfile;
		vhdfile=NULL;
	}
	else if(r_vhdfile!=NULL)
	{
		image_fak->destroyVHDFile(r_vhdfile);
	}
	if(hashfile!=NULL) Server->destroy(hashfile);
	if(parenthashfile!=NULL) Server->destroy(parenthashfile);
	delete []zeroblockdata;
	return false;
}

unsigned int ImageBackup::writeMBR(ServerVHDWriter* vhdfile, uint64 volsize)
{
	unsigned char *mbr=(unsigned char *)vhdfile->getBuffer();
	if(mbr==NULL)
		return 0;

	unsigned char *mptr=mbr;

	memcpy(mptr, mbr_code, 440);
	mptr+=440;
	int sig=Server->getRandomNumber();
	memcpy(mptr, &sig, sizeof(int));
	mptr+=sizeof(int);
	*mptr=0;
	++mptr;
	*mptr=0;
	++mptr;

	unsigned char partition[16];
	partition[0]=0x80;
	partition[1]=0xfe;
	partition[2]=0xff;
	partition[3]=0xff;
	partition[4]=0x07; //ntfs
	partition[5]=0xfe;
	partition[6]=0xff;
	partition[7]=0xff;
	partition[8]=0x00;
	partition[9]=0x04;
	partition[10]=0x00;
	partition[11]=0x00;

	unsigned int sectors=(unsigned int)(volsize/((uint64)sector_size));
	sectors = little_endian(sectors);
	memcpy(&partition[12], &sectors, sizeof(unsigned int) );

	memcpy(mptr, partition, 16);
	mptr+=16;
	for(int i=0;i<3;++i)
	{
		memset(mptr, 0, 16);
		mptr+=16;
	}
	*mptr=0x55;
	++mptr;
	*mptr=0xaa;
	vhdfile->writeBuffer(0, (char*)mbr, 512);

	for(int i=0;i<1023;++i)
	{
		char *buf=vhdfile->getBuffer();
		if(buf==NULL)
			return 0;

		memset(buf, 0, 512);
		vhdfile->writeBuffer((i+1)*512, buf, 512);
	}

	return 1024*512;
}

int64 ImageBackup::updateNextblock(int64 nextblock, int64 currblock, sha256_ctx *shactx, unsigned char *zeroblockdata, bool parent_fn,
	ServerVHDWriter *parentfile, IFile *hashfile, IFile *parenthashfile, unsigned int blocksize,
	int64 mbr_offset, int64 vhd_blocksize, bool& warned_about_parenthashfile_error, int64 empty_vhdblock_start)
{
	if(nextblock==currblock)
		return nextblock+1;
	else if(nextblock>currblock)
		return nextblock;

	if(currblock-nextblock>=vhd_blocksize)
	{
		if(nextblock%vhd_blocksize!=0)
		{
			while(true)
			{
				sha256_update(shactx, zeroblockdata, blocksize);
				++nextblock;

				if(nextblock%vhd_blocksize==0 && nextblock!=0)
				{
					unsigned char dig[sha_size];
					sha256_final(shactx, dig);
					hashfile->Write((char*)dig, sha_size);
					sha256_init(shactx);
					break;
				}
			}
		}

		while(currblock-nextblock>=vhd_blocksize)
		{
			if(!parent_fn || nextblock==empty_vhdblock_start)
			{
				hashfile->Write((char*)zero_hash, sha_size);
			}
			else
			{
				bool b=parenthashfile->Seek((nextblock/vhd_blocksize)*sha_size);
				if(!b)
				{
					if(!warned_about_parenthashfile_error)
					{
						Server->Log("Seeking in parent hash file failed (May be caused by a volume with increased size)", LL_WARNING);
						warned_about_parenthashfile_error=true;
					}
					hashfile->Write((char*)zero_hash, sha_size);
				}
				else
				{
					char dig[sha_size];
					_u32 rc=parenthashfile->Read(dig, sha_size);
					if(rc!=sha_size)
					{
						if(!warned_about_parenthashfile_error)
						{
							Server->Log("Reading from parent hash file failed (May be caused by a volume with increased size)", LL_WARNING);
							warned_about_parenthashfile_error=true;
						}
						hashfile->Write((char*)zero_hash, sha_size);
					}
					else
					{
						hashfile->Write(dig, sha_size);
					}
				}
			}
			nextblock+=vhd_blocksize;
		}
	}

	while(nextblock<currblock)
	{
		sha256_update(shactx, zeroblockdata, blocksize);
		++nextblock;
		if(nextblock%vhd_blocksize==0 && nextblock!=0)
		{
			unsigned char dig[sha_size];
			sha256_final(shactx, dig);
			hashfile->Write((char*)dig, sha_size);
			sha256_init(shactx);
		}
	}
	return nextblock+1;
}

std::string ImageBackup::constructImagePath(const std::string &letter, std::string image_file_format, std::string pParentvhd)
{
	bool full_backup = pParentvhd.empty();

	if (!createDirectoryForClient())
	{
		return std::string();
	}

	time_t tt=time(NULL);
#ifdef _WIN32
	tm lt;
	tm *t=&lt;
	localtime_s(t, &tt);
#else
	tm *t=localtime(&tt);
#endif
	char buffer[500];
	strftime(buffer, 500, "%y%m%d-%H%M", t);
	std::string backupfolder_uncompr=server_settings->getSettings()->backupfolder_uncompr;
	backuppath_single = std::string(buffer) + "_Image_" + letter;
	std::string image_folder = backupfolder_uncompr + os_file_sep() + clientname + os_file_sep() + backuppath_single;
	std::string imgpath = image_folder + os_file_sep() + "Image_"+letter+"_"+(std::string)buffer;
	bool create_folder = true;
	if(image_file_format==image_file_format_vhd)
	{
		imgpath+=".vhd";
	}
	else if(image_file_format==image_file_format_cowraw)
	{
		imgpath+=".raw";
		create_folder = false;
		if (full_backup)
		{
			if (!SnapshotHelper::createEmptyFilesystem(clientname, backuppath_single))
			{
				return std::string();
			}
		}
	}
	else
	{
		imgpath+=".vhdz";
	}

	if (create_folder)
	{
		if (!os_create_dir(os_file_prefix(image_folder)))
		{
			return std::string();
		}
	}

	if (!full_backup && image_file_format == image_file_format_cowraw)
	{
		std::string parent_backuppath_single = ExtractFileName(ExtractFilePath(pParentvhd));
		std::string parent_fn = ExtractFileName(pParentvhd);

		ServerLogger::Log(logid, "Creating writable snapshot of previous image backup...", LL_INFO);
		if (!SnapshotHelper::snapshotFileSystem(clientname, parent_backuppath_single, backuppath_single))
		{
			ServerLogger::Log(logid, "Could not create snapshot of previous image backup at " + parent_backuppath_single, LL_ERROR);
			return std::string();
		}
		else
		{
			Server->deleteFile(image_folder + os_file_sep() + parent_fn + ".hash");
			Server->deleteFile(image_folder + os_file_sep() + parent_fn + ".cbitmap");
			Server->deleteFile(image_folder + os_file_sep() + parent_fn + ".mbr");
			os_rename_file(image_folder + os_file_sep() + parent_fn + ".bitmap", imgpath+".bitmap");
			if (!os_rename_file(image_folder + os_file_sep() + parent_fn, imgpath))
			{
				ServerLogger::Log(logid, "Error renaming in snapshot (\"" + image_folder + os_file_sep() + parent_fn + "\" to \""+imgpath+"\")", LL_ERROR);
				return std::string();
			}
		}
	}

	int64 free_space = os_free_space(os_file_prefix(ExtractFilePath(imgpath)));
	if (free_space != -1 && free_space<minfreespace_image)
	{
		ServerLogger::Log(logid, "Not enough free space. Cleaning up.", LL_INFO);
		if (!ServerCleanupThread::cleanupSpace(minfreespace_image))
		{
			ServerLogger::Log(logid, "Could not free space for image. NOT ENOUGH FREE SPACE.", LL_ERROR);
			return std::string();
		}
	}

	return imgpath;
}

SBackup ImageBackup::getLastImage(const std::string &letter, bool incr)
{
	ServerBackupDao::SImageBackup image_backup;
	if(incr)
	{
		image_backup = backup_dao->getLastImage(clientid, client_main->getCurrImageVersion(), letter);
	}
	else
	{
		image_backup = backup_dao->getLastFullImage(clientid, client_main->getCurrImageVersion(), letter);
	}

	if(image_backup.exists)
	{
		SBackup b;
		b.incremental=image_backup.incremental;
		b.path=image_backup.path;
		b.incremental_ref=static_cast<int>(image_backup.id);
		b.backup_time_ms=image_backup.duration;
		return b;
	}
	else
	{
		SBackup b;
		b.incremental=-2;
		b.incremental_ref=0;
		return b;
	}
}

std::string ImageBackup::getMBR(const std::string &dl)
{
	std::string ret=client_main->sendClientMessage("MBR driveletter="+dl, "Getting MBR for drive "+dl+" failed", 10000);
	CRData r(&ret);
	char b;
	if(r.getChar(&b) && b==1 )
	{
		char ver;
		if(r.getChar(&ver) )
		{
			if(ver!=0 && ver!=1)
			{
				ServerLogger::Log(logid, "MBR version "+convert((int)ver)+" is not supported by this server", LL_ERROR);
			}
			else
			{
				CRData r2(&ret);
				SMBRData mbrdata(r2);
				if(!mbrdata.errmsg.empty())
				{
					ServerLogger::Log(logid, "During getting MBR: "+mbrdata.errmsg, LL_WARNING);
				}
				return ret;
			}
		}
		else
		{
			ServerLogger::Log(logid, "Could not read version information in MBR", LL_ERROR);
		}
	}
	else if(dl!="SYSVOL" && dl!="ESP")
	{
		std::string errmsg;
		if( r.getStr(&errmsg) && !errmsg.empty())
		{
			errmsg=". Error message: "+errmsg;
		}
		ServerLogger::Log(logid, "Could not read MBR"+errmsg, LL_ERROR);
	}

	return "";
}