#include "../../Interface/Action.h"

namespace Actions
{
	ACTION(progress);
	ACTION(login);
	ACTION(salt);
	ACTION(lastacts);
	ACTION(piegraph);
	ACTION(usagegraph);
	ACTION(usage);
	ACTION(users);
	ACTION(status);
	ACTION(backups);
	ACTION(settings);
	ACTION(logs);
	ACTION(getimage);
	ACTION(shutdown);
	ACTION(download_client);
	ACTION(livelog);
	ACTION(start_backup);
	ACTION(add_client);
	ACTION(restore_prepare_wait);
	ACTION(scripts);
	ACTION(status_check);
	ACTION(restore_image);
	ACTION(browse_dir); // HASERTI: picker de destino
	ACTION(flr_pull); // HASERTI: restaurar-na-maquina a partir da imagem (FLR)
}
