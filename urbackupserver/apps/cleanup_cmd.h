#include <string>

class IDatabase;

int cleanup_cmd(void);
int64 cleanup_amount(std::string cleanup_pc, IDatabase *db);
int remove_unknown(void);