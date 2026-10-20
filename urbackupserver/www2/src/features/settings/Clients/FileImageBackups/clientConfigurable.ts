import { urbackupServer } from "../../../../App";

// The server ignores the "client" source for all other settings
export function isClientConfigurable(name: string) {
  return (urbackupServer.clientSettingsList() as readonly string[]).includes(
    name,
  );
}
