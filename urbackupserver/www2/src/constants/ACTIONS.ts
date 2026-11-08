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
  UPDATE: "Client update",
  CHECK_INTEGRITY: "Checking database integrity",
  BACKUP_DATABASE: "Backing up database",
  RECALCULATE_STATISTICS: "Recalculating statistics",
  NIGHTLY_CLEANUP: "Nightly clean-up",
  EMERGENCY_CLEANUP: "Emergency clean-up",
  STORAGE_MIGRATION: "Storage migration",

  // Delete actions
  DEL_INCR_FILE: "Deleting incremental file backup",
  DEL_FULL_FILE: "Deleting full file backup",
  DEL_INCR_IMAGE: "Deleting incremental image backup",
  DEL_FULL_IMAGE: "Deleting full image backup",
} as const;
