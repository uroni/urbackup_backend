#pragma once
#include <string>
#include <map>
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"
#include "../fsimageplugin/IFilesystem.h"
#include "../common/bitmap.h"

class ClientConnector;
struct ImageInformation;
class ClientSend;

class ImageThread : public IThread, public IFsNextBlockCallback
{
public:
	ImageThread(ClientConnector *client, IPipe *pipe, IPipe *mempipe, ImageInformation *image_inf,
		std::string server_token, IFile *hashdatafile, IFile* bitmapfile);

	void operator()(void);

	static std::string hdatFn(std::string volume);
	static IFsFile* openHdatF(std::string volume, bool share);

	int64 nextBlock(int64 curr_block);

	virtual void slowReadWarning(int64 passed_time_ms, int64 curr_block);

	virtual void waitingForBlockCallback(int64 curr_block);

private:

	void ImageErr(const std::string &msg, int loglevel=LL_ERROR);
	void ImageErrRunning(std::string msg);

	void createShadowData(str_map& other_vols, CWData& shadow_data);

	bool sendFullImageThread(void);
	bool sendIncrImageThread(void);

	void removeShadowCopyThread(int save_id);
	void updateShadowCopyStarttime(int save_id);

	bool sendBitmap(IFilesystem* fs, int64 drivesize, unsigned int blocksize);
	std::string getFsErrMsg();

	IPipe *pipe;
	IPipe *mempipe;
	ClientConnector *client;
	std::string server_token;
	IFile *hashdatafile;
	IFile* bitmapfile;

	unsigned int blocks_per_vhdblock;
	Bitmap cbt_bitmap;

	IFilesystem* curr_fs;
	
	ImageInformation *image_inf;

	int64 lastsendtime;
	ClientSend* clientSend;
};