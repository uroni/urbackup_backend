#include <string>
#include <vector>

std::vector<std::string> getSettingsList(void);
std::vector<std::string> getClientConfigurableSettingsList(const bool with_max_backups_config);
std::vector<std::string> getClientMergableSettingsList();
std::vector<std::string> getOnlyServerClientSettingsList(void);
std::vector<std::string> getGlobalizedSettingsList(void);
std::vector<std::string> getLocalizedSettingsList(void);
std::vector<std::string> getGlobalSettingsList(void);
std::vector<std::string> getLdapSettingsList(void);