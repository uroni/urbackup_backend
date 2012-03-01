#include <string>
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../Interface/Thread.h"

class ClientConnector;
struct ImageInformation;

class ImageThread : public IThread
{
public:
	ImageThread(ClientConnector *client, IPipe *pipe, IPipe **mempipe, ImageInformation *image_inf, std::string server_token, IFile *hashdatafile);

	void operator()(void);

private:

	void ImageErr(const std::string &msg);
	void ImageErrRunning(const std::string &msg);

	void sendFullImageThread(void);
	void sendIncrImageThread(void);

	void removeShadowCopyThread(int save_id);

	IPipe *pipe;
	IPipe **mempipe;
	ClientConnector *client;
	std::string server_token;
	IFile *hashdatafile;
	
	ImageInformation *image_inf;
};