import { useMutation, useQueryClient } from "react-query";
import { urbackupServer } from "../../App";

export function useBackupMutation() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: startBackup,
    onSuccess: () => {
      queryClient.invalidateQueries("status");
    },
  });
}

async function startBackup({
  id,
  type,
}: {
  id: Parameters<typeof urbackupServer.startBackup>["0"];
  type: Parameters<typeof urbackupServer.startBackup>["1"];
}) {
  const resp = await urbackupServer.startBackup(id, type);

  return resp;
}
