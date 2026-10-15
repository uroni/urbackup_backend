import { describe, expect, test } from "vitest";

import {
  describe as describeBackups,
  parseCount,
  parseInterval,
  SECONDS_PER_DAY,
  SECONDS_PER_HOUR,
} from "./backupSummaryModel";

const fileSettings = {
  image: false,
  incr: 5 * SECONDS_PER_HOUR,
  full: 30 * SECONDS_PER_DAY,
  max_full: 10,
  min_full: 2,
  max_incr: 100,
  min_incr: 40,
  to_last: [false],
};

describe.concurrent("backup summary", () => {
  test.each([
    ["5", false, 5 * SECONDS_PER_HOUR],
    ["-5", false, -1],
    ["5", true, -1],
    ["", false, null],
    ["0", false, null],
    ["1-5/8-17", false, null],
    ["5;1@1-5/8-17", false, "varies"],
  ] as const)(
    "interval %s (disabled: %s) is %s",
    (text, disabled, expected) => {
      expect(parseInterval(text, disabled, SECONDS_PER_HOUR)).toBe(expected);
    },
  );

  test("counts", () => {
    expect(parseCount("12")).toBe(12);
    expect(parseCount(" 3 ")).toBe(3);
    expect(parseCount("-1")).toBeNull();
    expect(parseCount(undefined)).toBeNull();
  });

  test("default file backup settings", () => {
    const summary = describeBackups(fileSettings);

    expect(summary.warn).toEqual([]);
    expect(summary.info[0]).toBe(
      "A full file backup runs about every 30 days and an incremental file backup about every 5 hours in between.",
    );
    expect(summary.info).toContain(
      "If every scheduled backup runs, every backup of the last 20.6 days can be restored. Further back only some of them are left, the oldest one is about 9 months old.",
    );
  });

  test("full interval not longer than the incremental one", () => {
    const summary = describeBackups({
      ...fileSettings,
      full: 2 * SECONDS_PER_HOUR,
    });

    expect(summary.info[0]).toBe(
      "Every scheduled file backup is a full file backup, about every 5 hours.",
    );
    expect(summary.warn).toHaveLength(1);
  });

  test("min above max", () => {
    const summary = describeBackups({ ...fileSettings, min_incr: 200 });

    expect(summary.warn).toEqual([
      "The minimal number of incremental file backups (200) is larger than the maximal number (100). Never more than 100 are kept, and none of them can be deleted if the backup storage runs out of space.",
    ]);
  });

  test("image backups based on each other lose the newest ones", () => {
    const summary = describeBackups({
      image: true,
      incr: SECONDS_PER_DAY,
      full: 30 * SECONDS_PER_DAY,
      max_full: 5,
      min_full: 2,
      max_incr: 4,
      min_incr: 2,
      to_last: [true],
    });

    expect(summary.warn.join(" ")).toContain(
      "the cleanup deletes the newest incremental image backups",
    );
  });
});
