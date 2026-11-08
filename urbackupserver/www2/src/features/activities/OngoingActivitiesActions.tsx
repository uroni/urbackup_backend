import { Button } from "@fluentui/react-components";
import { OpenRegular } from "@fluentui/react-icons";
import { useMutation } from "@tanstack/react-query";

import type { ProcessItem } from "../../api/urbackupserver";
import { urbackupServer } from "../../App";

const stopButtonStyle = {
  minWidth: 0,
};

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

export function OngoingActivitiesActions({
  process,
}: {
  process: ProcessItem;
}) {
  const stopProcessMutation = useStopProcessMutation();

  return (
    <div className="cluster gutter-xs">
      {process.can_stop_backup && (
        <Button
          size="small"
          style={stopButtonStyle}
          onClick={() =>
            stopProcessMutation.mutate({
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
