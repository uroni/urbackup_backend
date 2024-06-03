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
  onBackup: (type: BackupType) => void;
  children: React.ReactNode;
}) {
  const handleBackup = (e: React.MouseEvent<HTMLDivElement>) => {
    const type = e.currentTarget.dataset.type;

    onBackup(+type!);
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
          <MenuItem data-type={BackupType.INCR_FILE} onClick={handleBackup}>
            Incremental file backup
          </MenuItem>
          <MenuItem data-type={BackupType.FULL_FILE} onClick={handleBackup}>
            Full file backup
          </MenuItem>
          <MenuItem data-type={BackupType.INCR_IMAGE} onClick={handleBackup}>
            Incremental image backup
          </MenuItem>
          <MenuItem data-type={BackupType.FULL_IMAGE} onClick={handleBackup}>
            Full image backup
          </MenuItem>
          {children}
        </MenuList>
      </MenuPopover>
    </Menu>
  );
}
