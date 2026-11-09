import { describe, expect, it } from "vitest";

import { serverVersionNumber } from "./serverVersion";

describe("serverVersionNumber", () => {
  it("encodes the version like the server's version info", () => {
    expect(serverVersionNumber("2.5.37 Rev. 0123abcd")).toBe(2005003700);
    expect(serverVersionNumber("2.6.0")).toBe(2006000000);
  });

  it("has no number without a release build", () => {
    expect(serverVersionNumber("$version_full$")).toBeUndefined();
    expect(serverVersionNumber(undefined)).toBeUndefined();
  });
});
