import { ActivityItem } from "../../api/urbackupserver";
import { ACTIONS } from "./ACTIONS";

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
