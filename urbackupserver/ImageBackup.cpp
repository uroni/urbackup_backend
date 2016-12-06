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
#include "server.h"

const unsigned int status_update_intervall=1000;
const unsigned int eta_update_intervall=60000;
const unsigned int sector_size=512;
const unsigned int sha_size=32;
const size_t minfreespace_image=1000*1024*1024; //1000 MB
const unsigned int image_timeout=10*24*60*60*1000;
const unsigned int image_recv_timeout=30*60*1000;
const unsigned int image_recv_timeout_after_first=2*60*1000;
const unsigned int mbr_size=(1024*1024)/2;
const int max_num_hash_errors = 10;

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
				ServerLogger::Log(logid, "Reading from file failed. "+os_last_error_str(), LL_ERROR);
				return ESendErr_Read;
			}
			if(!outputpipe->Write(send_buffer, tsend, 120000, false))
			{
				ServerLogger::Log(logid, "Sending file data failed", LL_DEBUG);
				return ESendErr_Send;
			}
		}
		
		if (!outputpipe->Flush(120000))
		{
			ServerLogger::Log(logid, "Flushing file data failed", LL_DEBUG);
			return ESendErr_Send;
		}

		return ESendErr_Ok;
	}
}

ImageBackup::ImageBackup(ClientMain* client_main, int clientid, std::string clientname,
	std::string clientsubname, LogAction log_action, bool incremental, std::string letter, std::string server_token, std::string details,
	bool set_complete, int64 snapshot_id, std::string snapshot_group_loginfo, int64 backup_starttime, bool scheduled)
	: Backup(client_main, clientid, clientname, clientsubname, log_action, false, incremental, server_token, details, scheduled),
	pingthread_ticket(ILLEGAL_THREADPOOL_TICKET), letter(letter), synthetic_full(false), backupid(0), not_found(false),
	set_complete(set_complete), snapshot_id(snapshot_id), mutex(Server->createMutex()),
	snapshot_group_loginfo(snapshot_group_loginfo), backup_starttime(backup_starttime)
{
}

std::vector<ImageBackup::SImageDependency> ImageBackup::getDependencies(bool reset)
{
	IScopedLock lock(mutex.get());
	std::vector<ImageBackup::SImageDependency> ret = dependencies;
	if (reset)
	{
		dependencies.clear();
	}
	return ret;
}

bool ImageBackup::doBackup()
{
	bool cowraw_format = server_settings->getImageFileFormat()==image_file_format_cowraw;

	if(r_incremental)
	{
		ServerLogger::Log(logid, std::string("Starting ")+(scheduled?"scheduled" : "unscheduled")+ " incremental image backup of volume \""+letter+"\"...", LL_INFO);
	}
	else
	{
		ServerLogger::Log(logid, std::string("Starting ") + (scheduled ? "scheduled" : "unscheduled") + " full image backup of volume \""+letter+"\"...", LL_INFO);

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

	if (!snapshot_group_loginfo.empty())
	{
		ServerLogger::Log(logid, "Image backup is being backed up in a snapshot group together with volumes " + snapshot_group_loginfo, LL_INFO);
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
	ScopedLockImageFromCleanup lock_cleanup_sysvol(0);
	ScopedLockImageFromCleanup lock_cleanup_esp(0);
	if(strlower(letter)=="c:")
	{
		ServerLogger::Log(logid, "Backing up SYSVOL...", LL_DEBUG);
		ImageBackup sysvol_backup(client_main, clientid, clientname, clientsubname, LogAction_NoLogging,
			false, "SYSVOL", server_token, "SYSVOL", false, 0, std::string(), 0, scheduled);
		sysvol_backup.setStopBackupRunning(false);
		sysvol_backup();

		if(sysvol_backup.getResult())
		{
			sysvol_id = sysvol_backup.getBackupId();
			lock_cleanup_sysvol.reset(sysvol_id);
		}
		else if (!sysvol_backup.getNotFound()
			&& client_main->getProtocolVersions().efi_version > 0)
		{
			setErrors(sysvol_backup);
			ServerLogger::Log(logid, "Backing up System Reserved (SYSVOL) partition failed. Image backup failed", LL_ERROR);
			return false;
		}
	
		ServerLogger::Log(logid, "Backing up SYSVOL done.", LL_DEBUG);

		if(client_main->getProtocolVersions().efi_version>0)
		{
			ServerLogger::Log(logid, "Backing up EFI System Partition...", LL_DEBUG);
			ImageBackup esp_backup(client_main, clientid, clientname, clientsubname, LogAction_NoLogging,
				false, "ESP", server_token, "ESP", false, 0, std::string(), 0, scheduled);
			esp_backup.setStopBackupRunning(false);
			esp_backup();

			if(esp_backup.getResult())
			{
				esp_id = esp_backup.getBackupId();
				lock_cleanup_esp.reset(esp_id);
			}
			else if (!esp_backup.getNotFound()
				&& client_main->getProtocolVersions().efi_version > 0)
			{
				setErrors(esp_backup);
				ServerLogger::Log(logid, "Backing up EFI System Partition failed. Image backup failed", LL_ERROR);
				return false;
			}

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
					client_main->getProtocolVersions().client_bitmap_version>0,
				client_main->getProtocolVersions().require_previous_cbitmap>0);
		}
		else
		{
			ret = doImage(letter, last.path, last.incremental+1,
				cowraw_format?0:last.incremental_ref, image_hashed_transfer, server_settings->getImageFileFormat(),
				client_main->getProtocolVersions().client_bitmap_version>0,
				client_main->getProtocolVersions().require_previous_cbitmap>0);
		}
	}
	else
	{
		ret = doImage(letter, "", 0, 0, image_hashed_transfer, server_settings->getImageFileFormat(),
			      client_main->getProtocolVersions().client_bitmap_version>0,
			client_main->getProtocolVersions().require_previous_cbitmap>0);
	}

	if(ret)
	{
		if(sysvol_id!=-1)
		{
			backup_dao->saveImageAssociation(backupid, sysvol_id);
			backup_dao->setImageBackupComplete(sysvol_id);
		}

		if(esp_id!=-1)
		{
			backup_dao->saveImageAssociation(backupid, esp_id);
			backup_dao->setImageBackupComplete(esp_id);
		}
	}

	if(ret
		&& letter!="SYSVOL"
		&& letter!="ESP"
		&& set_complete
		&& dependencies.empty() )
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


bool ImageBackup::doImage(const std::string &pLetter, const std::string &pParentvhd, int incremental, int incremental_ref,
	bool transfer_checksum, std::string image_file_format, bool transfer_bitmap, bool transfer_prev_cbitmap)
{
	std::string sletter = pLetter;
	if (pLetter != "SYSVOL" && pLetter != "ESP")
	{
		sletter = pLetter[0];
	}

	std::string imagefn;
	bool fatal_mbr_error;
	std::string mbrd = getMBR(sletter, fatal_mbr_error);
	if (mbrd.empty())
	{
		if (pLetter != "SYSVOL" && pLetter != "ESP")
		{
			if (fatal_mbr_error)
			{
				ServerLogger::Log(logid, "Cannot retrieve master boot record (MBR) for the disk from the client.", LL_ERROR);
				return false;
			}
			else
			{
				ServerLogger::Log(logid, "Cannot retrieve master boot record (MBR) for the disk from the client. "
					"Continuing backup but you will not be able to restore this image backup via restore CD.", LL_WARNING);
			}
		}
	}
	else
	{
		imagefn = constructImagePath(sletter, image_file_format, pParentvhd);

		if (imagefn.empty())
		{
			return false;
		}

		std::auto_ptr<IFile> mbr_file(Server->openFile(os_file_prefix(imagefn + ".mbr"), MODE_WRITE));
		if (mbr_file.get() != NULL)
		{
			_u32 w = mbr_file->Write(mbrd);
			if (w != mbrd.size())
			{
				ServerLogger::Log(logid, "Error writing mbr data. " + os_last_error_str(), LL_ERROR);
				return false;
			}
			mbr_file.reset();
		}
		else
		{
			ServerLogger::Log(logid, "Error creating file for writing MBR data. " + os_last_error_str(), LL_ERROR);
			return false;
		}
	}

	ScopedLockImageFromCleanup cleanup_lock(0);

	addBackupToDatabase(pLetter, pParentvhd, incremental,
		incremental_ref, imagefn, cleanup_lock, NULL);

	CTCPStack tcpstack(client_main->isOnInternetConnection());
	IPipe *cc=client_main->getClientCommandConnection(10000);
	if(cc==NULL)
	{
		ServerLogger::Log(logid, "Connecting to \""+clientname+"\" for image backup failed", LL_ERROR);
		has_timeout_error = true;
		return false;
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

	std::string identity = client_main->getIdentity();

	chksum_str += "&running_jobs=" + convert(ServerStatus::numRunningJobs(clientname));

	if (snapshot_id != 0)
	{
		chksum_str += "&shadowid=" + convert(snapshot_id);
	}

	if(pParentvhd.empty())
	{
		tcpstack.Send(cc, identity+"FULL IMAGE letter="+pLetter+"&token="+server_token+chksum_str);
	}
	else
	{
		std::auto_ptr<IFile> hashfile(Server->openFile(os_file_prefix(pParentvhd + ".hash")));
		if(hashfile.get()==NULL)
		{
			ServerLogger::Log(logid, "Error opening hashfile ("+ pParentvhd + ".hash). "+os_last_error_str(), LL_ERROR);
			Server->Log("Starting image path repair...", LL_INFO);
			ServerUpdateStats::repairImages();
			Server->destroy(cc);
			return false;
		}
		std::auto_ptr<IFile> prevbitmap;
		std::string prevbitmap_str;
		if (transfer_prev_cbitmap)
		{
			prevbitmap.reset(Server->openFile(os_file_prefix(pParentvhd + ".cbitmap")));
			if (prevbitmap.get() == NULL)
			{
				ServerLogger::Log(logid, "Error opening previous bitmap ("+ pParentvhd + ".cbitmap). "+os_last_error_str()+". Backup will proceed without CBT.", LL_WARNING);
			}
			else
			{
				prevbitmap_str = "&cbitmapsize=" + convert(prevbitmap->Size());
			}
		}
		
		std::string ts=identity+"INCR IMAGE letter="+pLetter+"&hashsize="+convert(hashfile->Size())+"&token="+server_token+chksum_str+ prevbitmap_str;
		size_t rc=tcpstack.Send(cc, ts);
		if(rc==0)
		{
			ServerLogger::Log(logid, "Sending 'INCR IMAGE' command failed", LL_ERROR);
			Server->destroy(cc);
			return false;
		}
		ESendErr senderr = sendFileToPipe(hashfile.get(), cc, logid);
		if(senderr!=ESendErr_Ok)
		{
			if (senderr == ESendErr_Send)
			{
				ServerLogger::Log(logid, "Exchanging data with client during image backup preparation failed. Connection to client broke.", LL_ERROR);
				has_timeout_error = true;
			}
			else
			{
				ServerLogger::Log(logid, "Exchanging data with client during image backup preparation failed. Server cannot read necessary data.", LL_ERROR);
			}
			Server->destroy(cc);
			return false;
		}

		if (prevbitmap.get() != NULL)
		{
			ESendErr senderr = sendFileToPipe(prevbitmap.get(), cc, logid);
			if (senderr != ESendErr_Ok)
			{
				if (senderr == ESendErr_Send)
				{
					ServerLogger::Log(logid, "Exchanging data with client during image backup preparation failed. Connection to client broke.", LL_ERROR);
					has_timeout_error = true;
				}
				else
				{
					ServerLogger::Log(logid, "Exchanging data with client during image backup preparation failed. Server cannot read necessary data.", LL_ERROR);
				}
				Server->destroy(cc);
				return false;
			}
		}
	}

	std::string ret;
	int64 starttime=Server->getTimeMS();
	bool first=true;
	const unsigned int c_buffer_size=32768;
	std::vector<char> buffer;
	buffer.resize(c_buffer_size);
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
	bool persistent=false;
	std::vector<unsigned char> zeroblockdata;
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
			curr_image_recv_timeout = image_recv_timeout;
			ServerStatus::setProcessSpeed(clientname, status_id, 0);
			ServerStatus::setProcessEta(clientname, status_id, -1);
			if(persistent && nextblock!=0)
			{
				int64 continue_block=nextblock;
				if(continue_block%vhd_blocksize!=0 )
				{
					continue_block=(continue_block/vhd_blocksize)*vhd_blocksize;
				}
				else if (with_checksum 
					&& continue_block != (last_verified_block+ vhd_blocksize)
					&& continue_block != last_verified_block)
				{
					//We haven't received a checksum for this block yet. Start one block back.
					continue_block = ((continue_block / vhd_blocksize)-1)*vhd_blocksize;
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
						Server->Log("Image client is connected via Internet now. Waiting for client to return to local network...", LL_DEBUG);
						Server->wait(60000);
					}
					else
					{
						Server->Log("Trying to reconnect in doImage", LL_DEBUG);
						cc = client_main->getClientCommandConnection(10000);
						if (cc == NULL)
						{
							Server->wait(60000);
						}
						else
						{
							Server->Log("Connected. Authenticating...", LL_DEBUG);
							if (!client_main->authenticateIfNeeded(false,
								internet_connection!=client_main->isOnInternetConnection()))
							{
								Server->destroy(cc);
								cc = NULL;
								Server->wait(60000);
							}
							else
							{
								identity = client_main->getIdentity();
								reconnected = true;
								ServerStatus::setROnline(clientname, true);
								Server->Log("Reconnected.", LL_DEBUG);
								internet_connection = client_main->isOnInternetConnection();
								break;
							}
						}
					}					
				}

				if(!reconnected)
				{
					ServerLogger::Log(logid, "Timeout while trying to reconnect", LL_ERROR);
					goto do_image_cleanup;
				}

				if(pParentvhd.empty())
				{
					std::string cmd = identity+"FULL IMAGE letter="+pLetter+"&start="+convert(continue_block)+"&shadowid="+convert(snapshot_id)+ "&status_id=" + convert(status_id);
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
					std::string ts = "INCR IMAGE letter="+pLetter+"&start=" + convert(continue_block) + "&shadowid=" + convert(snapshot_id) + "&hashsize="
						+ convert(parenthashfile->Size()) + "&status_id=" + convert(status_id);
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
					std::auto_ptr<IFile> prevbitmap;
					if (transfer_prev_cbitmap)
					{
						prevbitmap.reset(Server->openFile(os_file_prefix(pParentvhd + ".cbitmap")));

						if (prevbitmap.get() != NULL)
						{
							ts += "&cbitmapsize=" + convert(prevbitmap->Size());
						}
						else
						{
							ServerLogger::Log(logid, "Opening previous client bitmap ("+ pParentvhd + ".cbitmap) failed during reconnect. This might cause the backup to fail.", LL_WARNING);
						}
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
						ServerLogger::Log(logid, "Sending hash data failed", LL_DEBUG);
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

					
					if (prevbitmap.get() != NULL)
					{
						rc = sendFileToPipe(prevbitmap.get(), cc, logid);
						if (rc == ESendErr_Send)
						{
							ServerLogger::Log(logid, "Sending bitmap data failed", LL_DEBUG);
							transferred_bytes += cc->getTransferedBytes();
							transferred_bytes_real += cc->getRealTransferredBytes();
							Server->destroy(cc);
							cc = NULL;
							continue;
						}
						else if (rc != ESendErr_Ok)
						{
							ServerLogger::Log(logid, "Reading from bitmap file failed", LL_ERROR);
							goto do_image_cleanup;
						}
					}
				}
				off=0;
				starttime=Server->getTimeMS();

				blockleft=0;
				currblock=-1;
			}
			else
			{
				has_timeout_error = true;
				if (!persistent)
				{
					ServerLogger::Log(logid, "Client disconnected and image is not persistent (Timeout: " + (cc == NULL ? "unavailable" : convert(!cc->hasError()))+").", LL_ERROR);
				}
				else
				{
					ServerLogger::Log(logid, "Client disconnected before sending anything (Timeout: " + (cc == NULL ? "unavailable" : convert(!cc->hasError())) + ").", LL_ERROR);
				}
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
					memcpy(&blocksize, buffer.data(), sizeof(unsigned int) );
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

							if (err == "Need previous client bitmap")
							{
								client_main->updateCapa();
							}
						}
						else
						{
							int loglevel = LL_WARNING;

							if (err == "Not found")
							{
								loglevel = LL_DEBUG;
								not_found = true;
							}

							if(pLetter=="SYSVOL")
								ServerLogger::Log(logid, "Request of SYSVOL failed. Reason: "+err, loglevel);
							else
								ServerLogger::Log(logid, "Request of EFI System Partition failed. Reason: "+err, loglevel);
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

					zeroblockdata.resize(blocksize);

					if (imagefn.empty())
					{
						imagefn = constructImagePath(sletter, image_file_format, pParentvhd);

						if (imagefn.empty())
						{
							goto do_image_cleanup;
						}

						addBackupToDatabase(pLetter, pParentvhd, incremental,
							incremental_ref, imagefn, cleanup_lock, running_updater);
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

					vhdfile=new ServerVHDWriter(r_vhdfile, blocksize, 5000, clientid, server_settings->getSettings()->use_tmpfiles_images,
						mbr_offset, hashfile, vhd_blocksize*blocksize, logid, drivesize + (int64)mbr_size);
					vhdfile_ticket = Server->getThreadPool()->execute(vhdfile, "image backup writer");

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

					if (!has_parent || blockcnt<0)
					{
						int64 vhd_size = -1;
						if (has_parent && blockcnt < 0)
						{
							ServerLogger::Log(logid, "Change block tracking active. Max "+PrettyPrintBytes(-blockcnt*blocksize)+" have changed.", LL_INFO);
							vhd_size = mbr_offset + -blockcnt*blocksize;
						}
						else if (!has_parent && blockcnt>0)
						{
							r_vhdfile->setBackingFileSize(mbr_offset + blockcnt*blocksize);
							vhd_size = mbr_offset + blockcnt*blocksize;
						}

						if (vhd_size>0 && vhd_size >= 2040LL * 1024 * 1024 * 1024
							&& image_file_format != image_file_format_cowraw)
						{
							ServerLogger::Log(logid, "Data on volume is to large for VHD files with " + PrettyPrintBytes(vhd_size) +
								". VHD files have a maximum size of 2040GB. Please use another image file format.", LL_ERROR);
							goto do_image_cleanup;
						}

						ServerStatus::setProcessTotalBytes(clientname, status_id, (blockcnt<0 ? -blockcnt : blockcnt)*blocksize);
					}

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

				unsigned int shadowdata_size=0;
				csize+=sizeof(unsigned int);
				if(r>=csize)
				{
					memcpy(&shadowdata_size, &buffer[off], sizeof(unsigned int));
					shadowdata_size = little_endian(shadowdata_size);
					off+=sizeof(unsigned int);
					if(shadowdata_size>0)
					{
						csize+= shadowdata_size;
						if( r>=csize)
						{
							std::string shadowdata(&buffer[off], shadowdata_size);
							readShadowData(shadowdata);
							off+= shadowdata_size;
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
					int shadow_id;
					memcpy(&shadow_id, &buffer[off], sizeof(int));
					snapshot_id = little_endian(shadow_id);
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
									if(has_parent && blockcnt>0)
									{
										ServerStatus::setProcessDoneBytes(clientname, status_id, currblock*blocksize);
										ServerStatus::setProcessPcDone(clientname, status_id, 
											(int)(((double)currblock/(double)totalblocks)*100.0+0.5) );
									}
									else
									{
										ServerStatus::setProcessDoneBytes(clientname, status_id, numblocks*blocksize);
										ServerStatus::setProcessPcDone(clientname, status_id,
											(int)(((double)numblocks/(double)((blockcnt>0 ? blockcnt : -blockcnt)))*100.0+0.5) );
									}
								}
							}

							if(ctime- last_eta_update>eta_update_intervall)
							{
								last_eta_update = ctime;

								int64 rel_blocks = (has_parent && blockcnt>=0) ? currblock : numblocks;
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
											int64 remaining_blocks = ((has_parent && blockcnt >= 0) ? totalblocks : 
												((blockcnt>0 ? blockcnt : -blockcnt)) - rel_blocks);
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

							nextblock=updateNextblock(nextblock, currblock, &shactx, zeroblockdata.data(),
								has_parent, hashfile, parenthashfile,
								blocksize, mbr_offset, vhd_blocksize, warned_about_parenthashfile_error,
								-1, vhdfile, 0);

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
								ClientMain::sendMailToAdmins("Fatal error occurred during image backup", ServerLogger::getWarningLevelTextLogdata(logid));
								goto do_image_cleanup;
							}
						}
						else if(nextblock-currblock>vhd_blocksize)
						{
							if (num_hash_errors < max_num_hash_errors)
							{
								ServerLogger::Log(logid, "Block sent out of sequence. Expected block >=" + convert(nextblock - vhd_blocksize - 1) + " got " + convert(currblock) + ". Retrying...", LL_WARNING);
								transferred_bytes += cc->getTransferedBytes();
								Server->destroy(cc);
								cc = NULL;
								nextblock = last_verified_block;
								hashfile->Seek((nextblock / vhd_blocksize)*sha_size);
								++num_hash_errors;
								break;
							}
							else
							{
								ServerLogger::Log(logid, "Block sent out of sequence. Expected block >=" + convert(nextblock - vhd_blocksize - 1) + " got " + convert(currblock) + ". Stopping backup.", LL_ERROR);
								goto do_image_cleanup;
							}
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
							ServerStatus::setProcessPcDone(clientname, status_id,
								100);
							ServerStatus::setProcessEta(clientname, status_id, -1);
							ServerStatus::setProcessSpeed(clientname, status_id, 0);

							if(nextblock<=totalblocks)
							{
								nextblock=updateNextblock(nextblock, totalblocks, &shactx, zeroblockdata.data(), has_parent,
									hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize, warned_about_parenthashfile_error,
									-1, vhdfile, 0);

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
								std::auto_ptr<IFile> sync_f;
								if (!vhdfile_err)
								{
									if (!os_sync(imagefn))
									{
										ServerLogger::Log(logid, "Syncing file system failed. Image backup may not be completely on disk. " + os_last_error_str(), LL_DEBUG);
									}

									sync_f.reset(Server->openFile(os_file_prefix(imagefn + ".sync"), MODE_WRITE));
								}

								int64 image_size = t_file->RealSize();
								db->BeginWriteTransaction();
								backup_dao->setImageSize(image_size, backupid);
								backup_dao->addImageSizeToClient(clientid, image_size);
								if(!vhdfile_err)
								{
									if (dependencies.empty())
									{
										backup_dao->setImageBackupComplete(backupid);
									}
									if (sync_f.get() != NULL)
									{
										backup_dao->setImageBackupSynctime(backupid);
									}
									else
									{
										ServerLogger::Log(logid, "Error creating sync file at " + imagefn + ".sync", LL_WARNING);
									}
									db->EndTransaction();

									if (image_file_format == image_file_format_cowraw)
									{
										if(!SnapshotHelper::makeReadonly(clientname, backuppath_single))
										{
											ServerLogger::Log(logid, "Making image backup snapshot read only failed", LL_WARNING);
										}
									}
								}
								else
								{
									db->EndTransaction();
								}
								
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

							if (!runPostBackupScript(!pParentvhd.empty() && !synthetic_full && incremental != 0,
								imagefn, pLetter, !vhdfile_err))
							{
								return false;
							}

							return !vhdfile_err;
						}
						else if(currblock==-124 ||
							currblock==0xFFFFFFFFFFFFFFFFLLU)
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

								ServerLogger::Log(logid, "Error on client occurred: "+err, LL_ERROR);
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
										nextblock=updateNextblock(nextblock, hblock-1, &shactx, zeroblockdata.data(), has_parent,
											hashfile, parenthashfile, blocksize, mbr_offset,
											vhd_blocksize, warned_about_parenthashfile_error, -1, vhdfile, 1);
										sha256_update(&shactx, zeroblockdata.data(), blocksize);						
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
									if(num_hash_errors<max_num_hash_errors)
									{
										ServerLogger::Log(logid, "Checksum for image block wrong. Retrying...", LL_WARNING);
										transferred_bytes+=cc->getTransferedBytes();
										Server->destroy(cc);
										cc=NULL;
										nextblock=last_verified_block;
										hashfile->Seek((nextblock / vhd_blocksize)*sha_size);
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
								nextblock = updateNextblock(nextblock, vhdblock+vhd_blocksize, &shactx, zeroblockdata.data(), has_parent,
									hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize, warned_about_parenthashfile_error,
									vhdblock, vhdfile, 0);
							}
							else
							{
								accum=true;
							}
							currblock=-1;
						}
						else if(currblock<0
							|| currblock>totalblocks)
						{
							if (num_hash_errors < max_num_hash_errors)
							{
								ServerLogger::Log(logid, "Received unknown block number: " + convert(currblock) + " (max: "+convert(totalblocks)+"). Retrying...", LL_WARNING);
								transferred_bytes += cc->getTransferedBytes();
								Server->destroy(cc);
								cc = NULL;
								nextblock = last_verified_block;
								hashfile->Seek((nextblock / vhd_blocksize)*sha_size);
								++num_hash_errors;
								break;
							}
							else
							{
								ServerLogger::Log(logid, "Received unknown block number: " + convert(currblock) + +" (max: " + convert(totalblocks) + "). Stopping backup.", LL_ERROR);
								goto do_image_cleanup;
							}
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
							memmove(buffer.data(), &buffer[off], r-off);
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
					if (available > 0)
					{
						memcpy(&blockdata[blocksize - blockleft], &buffer[off], available);
						blockleft -= available;
						off += available;
					}

					if( off>=r
						&& blockleft>0
						&& currblock!=-1)
					{
						off=0;
						break;
					}
				}
			}
		}
	}
	has_timeout_error = true;
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

	runPostBackupScript(!pParentvhd.empty() && !synthetic_full && incremental!=0, imagefn, pLetter, false);

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
	IFile *hashfile, IFile *parenthashfile, unsigned int blocksize,
	int64 mbr_offset, int64 vhd_blocksize, bool& warned_about_parenthashfile_error, int64 empty_vhdblock_start,
	ServerVHDWriter* vhdfile, int64 trim_add)
{
	if(trim_add>0
		&& vhdfile != NULL
		&& parent_fn
	    && (nextblock==currblock) )
	{
		vhdfile->writeBuffer(mbr_offset + nextblock*blocksize, NULL,
			    static_cast<unsigned int>(trim_add*blocksize));
	}
	
	if(nextblock==currblock)
		return nextblock+1;
	else if(nextblock>currblock)
		return nextblock;

	if(currblock-nextblock>=vhd_blocksize)
	{
		if(nextblock%vhd_blocksize!=0)
		{
			int64 trim_start_block = -1;

			while(true)
			{
				if (trim_start_block == -1)
				{
					trim_start_block = nextblock;
				}

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

			if (trim_start_block != -1
				&& vhdfile!=NULL
				&& parent_fn )
			{
				vhdfile->writeBuffer(mbr_offset + trim_start_block*blocksize, NULL,
					static_cast<unsigned int>((nextblock - trim_start_block)*blocksize));
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
						Server->Log("Seeking in parent hash file failed (may be caused by a volume with increased size)", LL_WARNING);
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
							Server->Log("Reading from parent hash file failed (may be caused by a volume with increased size)", LL_WARNING);
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

	int64 trim_start_block = -1;

	while(nextblock<currblock)
	{
		if (trim_start_block == -1)
		{
			trim_start_block = nextblock;
		}

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
	
	if(trim_add>0
	    && trim_start_block==-1)
	{
		trim_start_block = nextblock;
	}

	if (trim_start_block != -1
		&& vhdfile!=NULL
		&& parent_fn )
	{
		vhdfile->writeBuffer(mbr_offset + trim_start_block*blocksize, NULL,
			static_cast<unsigned int>((nextblock - trim_start_block + trim_add)*blocksize));
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
			if (BackupServer::getSnapshotMethod() == BackupServer::ESnapshotMethod_Zfs)
			{
				std::auto_ptr<IFile> touch_f(Server->openFile(image_folder, MODE_WRITE));
				if (touch_f.get()==NULL)
				{
					ServerLogger::Log(logid, "Could not touch file " + image_folder + ". " + os_last_error_str(), LL_ERROR);
					return std::string();
				}
				touch_f->Sync();
			}

			if (!SnapshotHelper::createEmptyFilesystem(clientname, backuppath_single))
			{
				if (BackupServer::getSnapshotMethod() == BackupServer::ESnapshotMethod_Zfs)
				{
					Server->deleteFile(image_folder);
				}
				return std::string();
			}
			else if (BackupServer::getSnapshotMethod() == BackupServer::ESnapshotMethod_Zfs)
			{
				std::string mountpoint = SnapshotHelper::getMountpoint(clientname, backuppath_single);
				if (mountpoint.empty())
				{
					ServerLogger::Log(logid, "Could not find mountpoint of snapshot of client " + clientname + " path " + backuppath_single, LL_ERROR);
					return std::string();
				}

				if (!os_link_symbolic(mountpoint, image_folder+"_new"))
				{
					ServerLogger::Log(logid, "Could create symlink to mountpoint at " + image_folder + " to " + mountpoint+". "+os_last_error_str(), LL_ERROR);
					return std::string();
				}

				if (!os_rename_file(image_folder + "_new", image_folder))
				{
					ServerLogger::Log(logid, "Could rename symlink at " + image_folder + "_new to " + image_folder + ". " + os_last_error_str(), LL_ERROR);
					return std::string();
				}
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

		if (BackupServer::getSnapshotMethod() == BackupServer::ESnapshotMethod_Zfs)
		{
			std::auto_ptr<IFile> touch_f(Server->openFile(image_folder, MODE_WRITE));
			if(touch_f.get()==NULL)
			{
				ServerLogger::Log(logid, "Could not touch file " + image_folder + ". "+os_last_error_str(), LL_ERROR);
				return std::string();
			}
			touch_f->Sync();
		}

		ServerLogger::Log(logid, "Creating writable snapshot of previous image backup...", LL_INFO);
		if (!SnapshotHelper::snapshotFileSystem(clientname, parent_backuppath_single, backuppath_single))
		{
			ServerLogger::Log(logid, "Could not create snapshot of previous image backup at " + parent_backuppath_single, LL_ERROR);
			if (BackupServer::getSnapshotMethod() == BackupServer::ESnapshotMethod_Zfs)
			{
				Server->deleteFile(image_folder);
			}
			return std::string();
		}
		else
		{
			if (BackupServer::getSnapshotMethod() == BackupServer::ESnapshotMethod_Zfs)
			{
				std::string mountpoint = SnapshotHelper::getMountpoint(clientname, backuppath_single);
				if (mountpoint.empty())
				{
					ServerLogger::Log(logid, "Could not find mountpoint of snapshot of client " + clientname+ " path "+ backuppath_single, LL_ERROR);
					return std::string();
				}

				if (!os_link_symbolic(mountpoint, image_folder+"_new"))
				{
					ServerLogger::Log(logid, "Could create symlink to mountpoint at " + image_folder + " to " + mountpoint+". "+os_last_error_str(), LL_ERROR);
					return std::string();
				}

				if (!os_rename_file(image_folder + "_new", image_folder))
				{
					ServerLogger::Log(logid, "Could rename symlink at " + image_folder + "_new to " + image_folder + ". " + os_last_error_str(), LL_ERROR);
					return std::string();
				}
			}

			Server->deleteFile(image_folder + os_file_sep() + parent_fn + ".hash");
			Server->deleteFile(image_folder + os_file_sep() + parent_fn + ".cbitmap");
			Server->deleteFile(image_folder + os_file_sep() + parent_fn + ".mbr");
			Server->deleteFile(image_folder + os_file_sep() + parent_fn + ".sync");
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

std::string ImageBackup::getMBR(const std::string &dl, bool& fatal_error)
{
	fatal_error = true;
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
			if (errmsg.find("dynamic volume")!=std::string::npos)
			{
				fatal_error = false;
			}

			errmsg=". Error message: "+errmsg;
		}
		ServerLogger::Log(logid, "Could not read MBR"+errmsg, LL_ERROR);
	}

	return "";
}

bool ImageBackup::runPostBackupScript(bool incr, const std::string& path, const std::string &pLetter, bool success)
{
	std::string script_name;
	if (!incr)
	{
		script_name = "post_full_imagebackup";
	}
	else
	{
		script_name = "post_incr_imagebackup";
	}

	return ClientMain::run_script("urbackup" + os_file_sep() + script_name,
		"\""+ path+"\" \"" + pLetter + "\" " + (success ? "1" : "0"), logid);
}

void ImageBackup::addBackupToDatabase(const std::string &pLetter, const std::string &pParentvhd, int incremental, int incremental_ref,
	const std::string& imagefn, ScopedLockImageFromCleanup& cleanup_lock, ServerRunningUpdater *running_updater)
{
	if (imagefn.empty())
	{
		return;
	}

	if (backup_starttime <= 0)
	{
		backup_starttime = Server->getTimeSeconds();
	}

	if (pParentvhd.empty())
	{
		backup_dao->newImageBackup(clientid, imagefn, 0, 0, client_main->getCurrImageVersion(), pLetter, backup_starttime);
	}
	else
	{
		backup_dao->newImageBackup(clientid, imagefn, synthetic_full ? 0 : incremental, incremental_ref, client_main->getCurrImageVersion(), pLetter, backup_starttime);
	}

	backupid = static_cast<int>(db->getLastInsertID());
	cleanup_lock.reset(backupid);
	if (running_updater != NULL)
	{
		running_updater->setBackupid(backupid);
	}
}

bool ImageBackup::readShadowData(const std::string & shadowdata)
{
	CRData rdata(shadowdata.data(), shadowdata.size());
	char version=-1;
	if (!rdata.getChar(&version) || version != 0)
	{
		if (version != 92) //sent by clients < version 2.1
		{
			ServerLogger::Log(logid, "Unknown shadow data version: " + convert(version), LL_ERROR);
		}
		return false;
	}

	IScopedLock lock(mutex.get());

	SImageDependency dep;
	while (rdata.getStr(&dep.volume)
		&& rdata.getVarInt(&dep.snapshot_id))
	{
		dependencies.push_back(dep);

		if (!snapshot_group_loginfo.empty())
		{
			snapshot_group_loginfo += ", ";
		}

		snapshot_group_loginfo += dep.volume;
	}

	ServerLogger::Log(logid, "Image backup is being backed up in a snapshot group together with volumes " + snapshot_group_loginfo, LL_INFO);

	client_main->sendToPipe("WAKEUP");
	
	return true;
}
