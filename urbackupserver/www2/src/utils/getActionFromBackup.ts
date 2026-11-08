import { ACTIONS } from "../constants/ACTIONS";

export function getActionFromBackup(backup: {
  restore: number; // !=0 if this is a restore
  image: number; // !=0 if this is a image backup
  resumed: number; // !=0 if this is a resumed backup
  incremental: number; // !=0 if this is a incremental backup
  del?: boolean; // This was a deletion activitiy or not
}): string {
  const isFileRestore = backup.restore !== 0 && backup.image === 0;
  const isImageRestore = backup.restore !== 0 && backup.image !== 0;

  if (isFileRestore) {
    // action = 8
    return ACTIONS.RESTORE_FILE;
  }

  if (isImageRestore) {
    // action - 9
    return ACTIONS.RESTORE_IMAGE;
  }

  const isFileBackup = backup.image === 0;
  const isImageBackup = backup.image !== 0;
  const isIncremental = backup.incremental > 0;
  const isDelBackup = backup.del;

  // TODO: Remove action variable comments,
  // if the refactored values from ACTIONS are approved
  if (isFileBackup) {
    const isResumableFileBackup = backup.resumed !== 0;

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
