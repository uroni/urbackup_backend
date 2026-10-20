import { describe, expect, it } from "vitest";

import {
  ARCHIVE_WINDOW_PATTERN,
  parseArchiveRules,
  serializeArchiveRules,
} from "./archiveRules";

const stored =
  "backup_type_c0=file&every_c0=86400&every_unit_c0=d&for_c0=604800&for_unit_c0=w&letters_c0=&uuid_c0=eb9ad287e82c95df711cee77aa7ea1ae&window_c0=*;*;*;*";

describe("archive rules", () => {
  it("parses the rules of a client", () => {
    expect(parseArchiveRules(stored, ["c"])).toEqual([
      {
        every: 1,
        everyUnit: "d",
        for: 1,
        forUnit: "w",
        backupType: "file",
        window: "*;*;*;*",
        letters: "",
        uuid: "eb9ad287e82c95df711cee77aa7ea1ae",
      },
    ]);
  });

  it("reads the next archival but doesn't save it", () => {
    const value = `${stored}&next_archival_c0=1792465714&timeleft_c0=3600`;
    const [rule] = parseArchiveRules(value, ["c"]);

    expect([rule.next, rule.timeLeft]).toEqual([1792465714, 3600]);
    expect(serializeArchiveRules([rule])).not.toContain("next_archival");
  });

  it("keeps the rules when saved again", () => {
    const again = serializeArchiveRules(parseArchiveRules(stored, ["c"]));
    expect(parseArchiveRules(again, ["c"])).toEqual(
      parseArchiveRules(stored, ["c"]),
    );
  });

  it("reads default and group rules in order", () => {
    const value =
      "every_d0=3600&every_unit_d0=h&for_d0=-1&for_unit_d0=i&backup_type_d0=image&window_d0=*;*;*;*&letters_d0=C" +
      "&every_g0=2628856&every_unit_g0=m&for_g0=31546267&for_unit_g0=y&backup_type_g0=file&window_g0=1;*;*;*&letters_g0=";
    const rules = parseArchiveRules(value, ["d", "g"]);

    expect(rules.map((r) => [r.every, r.everyUnit, r.for, r.forUnit])).toEqual([
      [1, "h", 0, "i"],
      [1, "m", 1, "y"],
    ]);
  });

  it("stores forever as -1", () => {
    const value = serializeArchiveRules([
      {
        every: 2,
        everyUnit: "w",
        for: 0,
        forUnit: "i",
        backupType: "full_image",
        window: "*;*;*;*",
        letters: "ALL",
      },
    ]);

    expect(Object.fromEntries(new URLSearchParams(value))).toEqual({
      every_c0: "1209600",
      every_unit_c0: "w",
      for_c0: "-1",
      for_unit_c0: "i",
      backup_type_c0: "full_image",
      window_c0: "*;*;*;*",
      letters_c0: "ALL",
    });
  });

  it("stores the general rules with their own prefix", () => {
    const general =
      "every_d0=3600&every_unit_d0=h&for_d0=-1&for_unit_d0=i&backup_type_d0=image&window_d0=*;*;*;*&letters_d0=C&uuid_d0=eb9ad287e82c95df711cee77aa7ea1ae";
    const again = serializeArchiveRules(parseArchiveRules(general, ["d"]), "d");

    expect(Object.fromEntries(new URLSearchParams(again))).toEqual(
      Object.fromEntries(new URLSearchParams(general)),
    );
  });

  it.each(["*;*;*;*", "3;1,15;*;*", "1,2,3;*;12;7"])("accepts window %s", (w) =>
    expect(ARCHIVE_WINDOW_PATTERN.test(w)).toBe(true),
  );

  it.each(["*;*;*", "a;*;*;*", ""])("rejects window %s", (w) =>
    expect(ARCHIVE_WINDOW_PATTERN.test(w)).toBe(false),
  );
});
