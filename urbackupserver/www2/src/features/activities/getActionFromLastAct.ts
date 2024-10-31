import { ActivityItem } from "../../api/urbackupserver";

export const ACTIONS = {
  NONE: "None",
  INCR_FILE: "Incremental file backup",
  FULL_FILE: "Full file backup",
  INCR_IMAGE: "Incremental image backup",
  FULL_IMAGE: "Full image backup",
  RESUME_INCR_FILE: "Resumed incremental file backup",
  RESUME_FULL_FILE: "Resumed full file backup",
  RESTORE_FILE: "File restore",
  RESTORE_IMAGE: "Image restore",

  // Unused 10 - 13
  // UPDATE: "Client update",
  // CHECK_INTEGRITY: "Checking database integrity",
  // BACKUP_DATABASE: "Backing up database",
  // RECALCULATE_STATISTICS: "Recalculating statistics",

  // Delete actions
  DEL_INCR_FILE: "Deleting incremental file backup",
  DEL_FULL_FILE: "Deleting full file backup",
  DEL_INCR_IMAGE: "Deleting incremental image backup",
  DEL_FULL_IMAGE: "Deleting full image backup",
} as const;

export function getActionFromLastAct(
  lastact: Pick<
    ActivityItem,
    "restore" | "image" | "resumed" | "incremental" | "del"
  >,
) {
  const isFileRestore = lastact.restore !== 0 && lastact.image === 0;
  const isImageRestore = lastact.restore !== 0 && lastact.image !== 0;

  if (isFileRestore) {
    // action = 8
    return ACTIONS.RESTORE_FILE;
  }

  if (isImageRestore) {
    // action - 9
    return ACTIONS.RESTORE_IMAGE;
  }

  const isFileBackup = lastact.image === 0;
  const isImageBackup = lastact.image !== 0;
  const isIncremental = lastact.incremental > 0;
  const isDelBackup = lastact.del;

  // TODO: Remove action variable comments,
  // if the refactored values from ACTIONS are approved
  if (isFileBackup) {
    const isResumableFileBackup = lastact.resumed !== 0;

    if (isResumableFileBackup) {
      // action = 5 | action = 6;
      return isIncremental
        ? ACTIONS.RESUME_INCR_FILE
        : ACTIONS.RESUME_FULL_FILE;
    }

    if (isIncremental) {
      // action = 1; action_1_d on delete
      return isDelBackup ? ACTIONS.DEL_INCR_FILE : ACTIONS.INCR_FILE;
    } else {
      // action = 2; action_2_d on delete
      return isDelBackup ? ACTIONS.DEL_FULL_FILE : ACTIONS.FULL_FILE;
    }
  }

  if (isImageBackup) {
    if (isIncremental) {
      // action = 3; action_3_d on delete
      return isDelBackup ? ACTIONS.DEL_INCR_IMAGE : ACTIONS.INCR_IMAGE;
    } else {
      // action = 4; action_4_d on delete
      return isDelBackup ? ACTIONS.DEL_FULL_IMAGE : ACTIONS.FULL_IMAGE;
    }
  }

  return ACTIONS.NONE;
}
