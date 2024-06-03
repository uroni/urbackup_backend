import {
  Menu,
  MenuButton,
  MenuItem,
  MenuList,
  MenuPopover,
  MenuTrigger,
} from "@fluentui/react-components";
import { MoreHorizontal24Filled } from "@fluentui/react-icons";

import { BackupType } from "../../api/urbackupserver";

export function StatusMenu({
  onBackup,
  children,
}: {
  onBackup: (e: React.MouseEvent<HTMLDivElement>) => void;
  children: React.ReactNode;
}) {
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
          <MenuItem data-type={BackupType.INCR_FILE} onClick={onBackup}>
            Incremental file backup
          </MenuItem>
          <MenuItem data-type={BackupType.FULL_FILE} onClick={onBackup}>
            Full file backup
          </MenuItem>
          <MenuItem data-type={BackupType.INCR_IMAGE} onClick={onBackup}>
            Incremental image backup
          </MenuItem>
          <MenuItem data-type={BackupType.FULL_IMAGE} onClick={onBackup}>
            Full image backup
          </MenuItem>
          {children}
        </MenuList>
      </MenuPopover>
    </Menu>
  );
}
