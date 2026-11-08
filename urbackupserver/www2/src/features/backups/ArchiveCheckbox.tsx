import { Caption1, Caption1Strong, Checkbox } from "@fluentui/react-components";
import { useMutation, useQueryClient } from "@tanstack/react-query";

import { Backup } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";
import { formatDatetime } from "../../utils/format";

function useArchiveBackupMutation() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      clientId,
      backupId,
    }: {
      clientId: number;
      backupId: number;
    }) => urbackupServer.archiveBackup(clientId, backupId),
    onSuccess: (_, variables) => {
      return queryClient.invalidateQueries({
        queryKey: ["backups", variables.clientId],
      });
    },
  });
}

function useUnarchiveBackupMutation() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      clientId,
      backupId,
    }: {
      clientId: number;
      backupId: number;
    }) => urbackupServer.unarchiveBackup(clientId, backupId),
    onSuccess: (_, variables) => {
      return queryClient.invalidateQueries({
        queryKey: ["backups", variables.clientId],
      });
    },
  });
}

export function ArchiveCheckbox(item: Backup & { clientid: number }) {
  const isArchived = item.archived === 1;

  const archiveBackupMutation = useArchiveBackupMutation();

  const unarchiveBackupMutation = useUnarchiveBackupMutation();

  return (
    <>
      <Checkbox
        checked={isArchived}
        onChange={(_, data) => {
          if (data.checked) {
            archiveBackupMutation.mutate({
              clientId: item.clientid,
              backupId: item.id,
            });

            return;
          }

          unarchiveBackupMutation.mutate({
            clientId: item.clientid,
            backupId: item.id,
          });
        }}
      />

      {!!item.archive_timeout && (
        <Caption1 block>
          Unarchives on{" "}
          <Caption1Strong block>
            {formatDatetime(item.archive_timeout)}
          </Caption1Strong>
        </Caption1>
      )}
    </>
  );
}
