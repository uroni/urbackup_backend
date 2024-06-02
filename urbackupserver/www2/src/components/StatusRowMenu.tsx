import {
  Menu,
  MenuButton,
  MenuItem,
  MenuList,
  MenuPopover,
  MenuTrigger,
} from "@fluentui/react-components";
import { MouseEvent } from "react";
import { MoreHorizontal24Filled } from "@fluentui/react-icons";
import { useMutation, useQueryClient } from "react-query";

import { urbackupServer } from "../App";
import { BackupType, StatusClientItem } from "../api/urbackupserver";

export function StatusRowMenu({ id }: { id: StatusClientItem["id"] }) {
  const queryClient = useQueryClient();

  const removeClientMutation = useMutation(urbackupServer.removeClient, {
    onSuccess: () => {
      queryClient.invalidateQueries("status");
    },
  });

  const startBackupMutation = useMutation(
    ({
      id,
      type,
    }: {
      id: Parameters<typeof urbackupServer.startBackup>["0"];
      type: Parameters<typeof urbackupServer.startBackup>["1"];
    }) => urbackupServer.startBackup(id, type),
  );

  const removeClient = () => {
    removeClientMutation.mutate(id);
  };

  const startBackup = (e: MouseEvent<HTMLDivElement>) => {
    const type = e.currentTarget.dataset.type;

    if (type) {
      startBackupMutation.mutate({ id: [id], type: +type });
    }
  };

  return (
    <Menu>
      <MenuTrigger disableButtonEnhancement>
        <MenuButton
          appearance="transparent"
          aria-label="More"
          icon={<MoreHorizontal24Filled />}
          onClick={(e) => e.stopPropagation()}
        />
      </MenuTrigger>

      <MenuPopover onClick={(e) => e.stopPropagation()}>
        <MenuList>
          <MenuItem data-type={BackupType.INCR_FILE} onClick={startBackup}>
            Incremental file backup
          </MenuItem>
          <MenuItem data-type={BackupType.FULL_FILE} onClick={startBackup}>
            Full file backup
          </MenuItem>
          <MenuItem data-type={BackupType.INCR_IMAGE} onClick={startBackup}>
            Incremental image backup
          </MenuItem>
          <MenuItem data-type={BackupType.FULL_IMAGE} onClick={startBackup}>
            Full image backup
          </MenuItem>
          <MenuItem onClick={removeClient}>Remove client</MenuItem>
        </MenuList>
      </MenuPopover>
    </Menu>
  );
}
