import { describe, expect, test } from "vitest";

import { getActionFromLastAct } from "./getActionFromLastAct";
import { ACTIONS } from "./ACTIONS";

const testCases: {
  lastact: Parameters<typeof getActionFromLastAct>[0];
  expected: (typeof ACTIONS)[keyof typeof ACTIONS];
}[] = [
  {
    lastact: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 1,
      del: false,
    },
    expected: ACTIONS.INCR_FILE,
  },
  {
    lastact: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.FULL_FILE,
  },
  {
    lastact: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 1,
      del: false,
    },
    expected: ACTIONS.INCR_IMAGE,
  },
  {
    lastact: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.FULL_IMAGE,
  },
  {
    lastact: {
      restore: 0,
      image: 0,
      resumed: 1,
      incremental: 1,
      del: false,
    },
    expected: ACTIONS.RESUME_INCR_FILE,
  },
  {
    lastact: {
      restore: 0,
      image: 0,
      resumed: 1,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.RESUME_FULL_FILE,
  },
  {
    lastact: {
      restore: 1,
      image: 0,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.RESTORE_FILE,
  },
  {
    lastact: {
      restore: 1,
      image: 1,
      resumed: 0,
      incremental: 0,
      del: false,
    },
    expected: ACTIONS.RESTORE_IMAGE,
  },
  {
    lastact: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 1,
      del: true,
    },
    expected: ACTIONS.DEL_INCR_FILE,
  },
  {
    lastact: {
      restore: 0,
      image: 0,
      resumed: 0,
      incremental: 0,
      del: true,
    },
    expected: ACTIONS.DEL_FULL_FILE,
  },
  {
    lastact: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 1,
      del: true,
    },
    expected: ACTIONS.DEL_INCR_IMAGE,
  },
  {
    lastact: {
      restore: 0,
      image: 1,
      resumed: 0,
      incremental: 0,
      del: true,
    },
    expected: ACTIONS.DEL_FULL_IMAGE,
  },
];

describe.concurrent("action from lastact", () => {
  test.each(testCases)(
    "lastact %# should be action: $expected",
    ({ lastact, expected }) => {
      expect(getActionFromLastAct(lastact)).toBe(expected);
    },
  );
});
