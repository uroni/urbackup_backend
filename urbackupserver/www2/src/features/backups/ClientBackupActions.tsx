import { tokens, Button, makeStyles } from "@fluentui/react-components";
import {
  Delete16Regular,
  PresenceOffline16Regular,
} from "@fluentui/react-icons";
import { useMutation, useQueryClient } from "@tanstack/react-query";

import type { Backup } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";

function useDeleteBackupMutation() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      clientId,
      backupId,
    }: {
      clientId: number;
      backupId: number;
    }) => urbackupServer.deleteBackup(clientId, backupId),
    onSuccess: (_, variables) => {
      return queryClient.invalidateQueries({
        queryKey: ["backups", variables.clientId],
      });
    },
  });
}

function useStopDeleteBackupMutation() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      clientId,
      backupId,
    }: {
      clientId: number;
      backupId: number;
    }) => urbackupServer.stopDeleteBackup(clientId, backupId),
    onSuccess: (_, variables) => {
      return queryClient.invalidateQueries({
        queryKey: ["backups", variables.clientId],
      });
    },
  });
}

function useDeleteBackupNowMutation() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      clientId,
      backupId,
    }: {
      clientId: number;
      backupId: number;
    }) => urbackupServer.deleteBackupNow(clientId, backupId),
    onSuccess: (_, variables) => {
      return queryClient.invalidateQueries({
        queryKey: ["backups", variables.clientId],
      });
    },
  });
}

const useStyles = makeStyles({
  root: {
    display: "flex",
    gap: tokens.spacingHorizontalXS,
  },
  stopButton: {
    minWidth: 0,
  },
});

export function ClientBackupActions(backup: Backup & { clientid: number }) {
  const deleteBackupMutation = useDeleteBackupMutation();
  const stopDeleteBackupMutation = useStopDeleteBackupMutation();
  const deleteBackupNowMutation = useDeleteBackupNowMutation();

  const classes = useStyles();

  if (backup.disable_delete || backup.archived) {
    return null;
  }

  if (backup.delete_pending) {
    return (
      <div className={classes.root}>
        <Button
          icon={<PresenceOffline16Regular />}
          onClick={() => {
            stopDeleteBackupMutation.mutate({
              clientId: backup.clientid,
              backupId: backup.id,
            });
          }}
        >
          Stop Delete
        </Button>
        <Button
          icon={<Delete16Regular />}
          onClick={() => {
            deleteBackupNowMutation.mutate({
              clientId: backup.clientid,
              backupId: backup.id,
            });
          }}
        >
          Delete Now
        </Button>
      </div>
    );
  }

  return (
    <div className={classes.root}>
      <Button
        icon={<Delete16Regular />}
        onClick={() => {
          deleteBackupMutation.mutate({
            clientId: backup.clientid,
            backupId: backup.id,
          });
        }}
      >
        Delete
      </Button>
    </div>
  );
}
