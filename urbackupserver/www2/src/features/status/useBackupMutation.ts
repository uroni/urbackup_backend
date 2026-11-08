import { useMutation, useQueryClient } from "@tanstack/react-query";
import { urbackupServer } from "../../App";

export function useBackupMutation() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: startBackup,
    onSuccess: async () => {
      await queryClient.invalidateQueries({
        queryKey: ["status"],
      });
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
