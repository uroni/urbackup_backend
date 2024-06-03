import { useMutation } from "react-query";
import { urbackupServer } from "../../App";

export function useBackupMutation() {
  return useMutation(
    ({
      id,
      type,
    }: {
      id: Parameters<typeof urbackupServer.startBackup>["0"];
      type: Parameters<typeof urbackupServer.startBackup>["1"];
    }) => urbackupServer.startBackup(id, type),
  );
}
