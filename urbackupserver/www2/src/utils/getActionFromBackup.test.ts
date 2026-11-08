import { describe, expect, test } from "vitest";

import { getActionFromBackup } from "./getActionFromBackup";
import { ACTIONS } from "../constants/ACTIONS";

const testCases: {
  backup: Parameters<typeof getActionFromBackup>[0];
  expected: (typeof ACTIONS)[keyof typeof ACTIONS];
}[] = [
  {
    backup: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 1,
      del: false,
    },
    expected: ACTIONS.INCR_FILE,
  },
  {
    backup: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.FULL_FILE,
  },
  {
    backup: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 1,
      del: false,
    },
    expected: ACTIONS.INCR_IMAGE,
  },
  {
    backup: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.FULL_IMAGE,
  },
  {
    backup: {
      restore: 0,
      image: 0,
      resumed: 1,
      incremental: 1,
      del: false,
    },
    expected: ACTIONS.RESUME_INCR_FILE,
  },
  {
    backup: {
      restore: 0,
      image: 0,
      resumed: 1,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.RESUME_FULL_FILE,
  },
  {
    backup: {
      restore: 1,
      image: 0,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.RESTORE_FILE,
  },
  {
    backup: {
      restore: 1,
      image: 1,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.RESTORE_IMAGE,
  },
  {
    backup: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 1,
      del: true,
    },
    expected: ACTIONS.DEL_INCR_FILE,
  },
  {
    backup: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 0,
      del: true,
    },
    expected: ACTIONS.DEL_FULL_FILE,
  },
  {
    backup: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 1,
      del: true,
    },
    expected: ACTIONS.DEL_INCR_IMAGE,
  },
  {
    backup: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 0,
      del: true,
    },
    expected: ACTIONS.DEL_FULL_IMAGE,
  },
];

describe.concurrent("action from backup", () => {
  test.each(testCases)(
    "%# should be action: $expected",
    ({ backup, expected }) => {
      expect(getActionFromBackup(backup)).toBe(expected);
    },
  );
});
