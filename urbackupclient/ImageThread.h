#pragma once
#include <string>
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"

class ClientConnector;
struct ImageInformation;
class IFilesystem;

class ImageThread : public IThread
{
public:
	ImageThread(ClientConnector *client, IPipe *pipe, IPipe *mempipe, ImageInformation *image_inf,
		std::string server_token, IFile *hashdatafile, IFile* bitmapfile);

	void operator()(void);

	static std::string hdatFn(std::string volume);
	static IFsFile* openHdatF(std::string volume, bool share);

private:

	void ImageErr(const std::string &msg, int loglevel=LL_ERROR);
	void ImageErrRunning(std::string msg);

	bool sendFullImageThread(void);
	bool sendIncrImageThread(void);

	void removeShadowCopyThread(int save_id);
	void updateShadowCopyStarttime(int save_id);

	bool sendBitmap(IFilesystem* fs, int64 drivesize, unsigned int blocksize);

	IPipe *pipe;
	IPipe *mempipe;
	ClientConnector *client;
	std::string server_token;
	IFile *hashdatafile;
	IFile* bitmapfile;
	
	ImageInformation *image_inf;
};