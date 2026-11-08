import { describe, expect, test } from "vitest";
import { formatUserRights } from "./formatUserRights";
import type { UserRight } from "../api/urbackupserver";

const baseRight: UserRight = {
  domain: "test",
  right: "test",
};

describe("action from backup", () => {
  test("should format single right", () => {
    const result = formatUserRights([baseRight]);

    expect(result).toBe("0_domain=test&0_right=test&idx=0");
  });

  test("should format for multiple rights", () => {
    const result = formatUserRights(Array.from({ length: 3 }, () => baseRight));

    expect(result).toBe(
      "0_domain=test&0_right=test,1_domain=test&1_right=test,2_domain=test&2_right=test&idx=0,1,2",
    );
  });
});
