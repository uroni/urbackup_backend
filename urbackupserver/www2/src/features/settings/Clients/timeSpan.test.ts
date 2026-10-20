import { describe, expect, it } from "vitest";

import {
  mapTimeSpan,
  MBIT_PER_S,
  SPEED_PATTERN,
  speedFormat,
  TIME_SPAN_PATTERN,
} from "./timeSpan";

describe("speedFormat", () => {
  const { ui, api } = speedFormat(MBIT_PER_S);

  it("shows no limit as '-'", () => {
    expect(ui("-1")).toBe("-");
    expect(ui("0")).toBe("-");
    expect(api("-")).toBe("0");
    expect(api("")).toBe("0");
  });

  it("converts plain values", () => {
    expect(ui("1310720")).toBe("10");
    expect(api("10")).toBe("1310720");
  });

  it("converts percentages", () => {
    expect(api("50%")).toBe("-51");
    expect(ui("-51")).toBe("50%");
  });

  it("converts time spans", () => {
    const stored = "1310720@1-5/8-17;-51";
    expect(ui(stored)).toBe("10@1-5/8-17;50%");
    expect(api(ui(stored))).toBe(stored);
  });

  it("round-trips odd values", () => {
    expect(api(ui("100000"))).toBe("100000");
  });
});

describe("SPEED_PATTERN", () => {
  it.each(["-", "", "10", "2.5", "50%", "10@Mon-Fri/8-17;50"])(
    "accepts %s",
    (v) => expect(SPEED_PATTERN.test(v)).toBe(true),
  );

  it.each(["fast", "10 MBit"])("rejects %s", (v) =>
    expect(SPEED_PATTERN.test(v)).toBe(false),
  );
});

describe("mapTimeSpan", () => {
  it.each([
    ["18000", "5"],
    ["18000@1-5/8-17;86400", "5@1-5/8-17;24"],
    ["-3600@Mon/1-2;7200", "-1@Mon/1-2;2"],
    ["", ""],
    ["3600;", "1;"],
  ])("converts %s seconds to %s hours", (seconds, hours) =>
    expect(mapTimeSpan(seconds, (v) => v / 3600)).toBe(hours),
  );

  it.each(["5", "5@1-5/8-17;24", "-1@Mon/1-2;-2", "0.5"])(
    "accepts the interval %s",
    (v) => expect(TIME_SPAN_PATTERN.test(v)).toBe(true),
  );

  it.each(["NaN", "5@1-5", "a"])("rejects the interval %s", (v) =>
    expect(TIME_SPAN_PATTERN.test(v)).toBe(false),
  );
});
