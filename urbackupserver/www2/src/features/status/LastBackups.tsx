import {
  Field,
  ProgressBar,
  TableCellLayout,
} from "@fluentui/react-components";
import {
  CheckmarkCircleFilled,
  DismissCircleFilled,
} from "@fluentui/react-icons";

import {
  ClientProcessActionTypes,
  ClientProcessItem,
  StartType,
  StatusClientItem,
} from "../../api/urbackupserver";
import { useBackupResult } from "./BackupResultContext";
import { formatDatetime } from "../../utils/format";

export function LastFileBackup(item: StatusClientItem) {
  const formattedLastBackup = formatLastBackup(item.lastbackup);

  const fileBackupProcesses = item.processes.filter(
    (p) =>
      [
        ClientProcessActionTypes.INCR_FILE,
        ClientProcessActionTypes.FULL_FILE,
      ].includes(p.action) && p.pcdone > -1,
  );

  const validBackups = ["incr_file", "full_file"] as StartType[];

  return (
    <TableCellLayout>
      <div>{formattedLastBackup}</div>
      <ProcessResult
        id={item.id}
        processes={fileBackupProcesses}
        validBackups={validBackups}
      />
    </TableCellLayout>
  );
}

export function LastImageBackup(item: StatusClientItem) {
  const formattedLastBackup = formatLastBackup(item.lastbackup_image);

  const imageBackupProcesses = item.processes.filter(
    (p) =>
      [
        ClientProcessActionTypes.INCR_IMAGE,
        ClientProcessActionTypes.FULL_IMAGE,
      ].includes(p.action) && p.pcdone > -1,
  );

  const validBackups = ["incr_image", "full_image"] as StartType[];

  return (
    <TableCellLayout>
      <div>{formattedLastBackup}</div>
      <ProcessResult
        id={item.id}
        processes={imageBackupProcesses}
        validBackups={validBackups}
      />
    </TableCellLayout>
  );
}

function ProcessResult({
  id,
  processes,
  validBackups,
}: {
  id: number;
  processes: ClientProcessItem[];
  validBackups: StartType[];
}) {
  const { getResultById } = useBackupResult();

  const result = getResultById(id);

  if (
    processes.length === 0 &&
    result &&
    validBackups.includes(result.start_type)
  ) {
    if (!result.start_ok) {
      return (
        <div className="cluster gutter-xs">
          <DismissCircleFilled className="icon fg-color-error" />
          Starting backup failed
        </div>
      );
    }

    return (
      <div className="cluster gutter-xs">
        <CheckmarkCircleFilled className="icon fg-color-success" />
        Queued backup
      </div>
    );
  }

  return processes.map((p) => {
    return (
      <Field
        key={p.action}
        validationMessage={`${p.pcdone}%`}
        validationState="none"
      >
        <ProgressBar max={100} value={p.pcdone} />
      </Field>
    );
  });
}

function formatLastBackup(
  lastBackup:
    | StatusClientItem["lastbackup"]
    | StatusClientItem["lastbackup_image"],
) {
  if (lastBackup === 0 || lastBackup === "-") {
    return "Never";
  }

  return formatDatetime(lastBackup);
}
