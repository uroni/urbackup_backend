import { tokens, Button, makeStyles } from "@fluentui/react-components";
import { OpenRegular } from "@fluentui/react-icons";
import { useMutation } from "@tanstack/react-query";

import type { ProcessItem } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";

function useStopProcessMutation() {
  return useMutation({
    mutationFn: ({
      clientId,
      processId,
    }: {
      clientId: number;
      processId: number;
    }) => urbackupServer.stopProcess(clientId, processId, false),
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

export function OngoingActivitiesActions({
  process,
}: {
  process: ProcessItem;
}) {
  const stopProcessMutatiion = useStopProcessMutation();

  const classes = useStyles();

  return (
    <div className={classes.root}>
      {process.can_stop_backup && (
        <Button
          size="small"
          className={classes.stopButton}
          onClick={() =>
            stopProcessMutatiion.mutate({
              clientId: process.clientid,
              processId: process.id,
            })
          }
        >
          Stop
        </Button>
      )}
      {process.can_show_backup_log && (
        <Button size="small" icon={<OpenRegular />}>
          Show Log
        </Button>
      )}
    </div>
  );
}
