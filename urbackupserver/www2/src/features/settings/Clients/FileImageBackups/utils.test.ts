import { describe, expect, test } from "vitest";

import { getEnabledState, getUse, SETTINGS_SOURCE, USE_VALUES } from "./utils";

const BASE_VALUES = [
  [SETTINGS_SOURCE.GROUP, USE_VALUES[SETTINGS_SOURCE.GROUP]],
  [SETTINGS_SOURCE.HERE, USE_VALUES[SETTINGS_SOURCE.HERE]],
  [SETTINGS_SOURCE.CLIENT, USE_VALUES[SETTINGS_SOURCE.CLIENT]],
] as const;

const testCases: [string, number][] = [
  // ["group, here, client", use]
  ["0,0,0", 0],
  ["1,0,0", 1],
  ["0,1,0", 2],
  ["1,1,0", 3],
  ["0,0,1", 4],
  ["1,0,1", 5],
  ["0,1,1", 6],
  ["1,1,1", 7],
];

const reversedTestCase = testCases.map(
  (ts) => ts.toReversed() as [number, string],
);

describe.concurrent("setting sources", () => {
  test.each(testCases)("%s should yield: %i", (state, expected) => {
    const builtState = buildState(state);
    expect(getUse(builtState)).toBe(expected);
  });

  test.each(reversedTestCase)("use: %i, expected: %s", (use, expected) => {
    const enabledState = getEnabledState(use)
      .map((es) => es.enabled)
      .join(",");

    expect(enabledState).toBe(expected);
  });
});

function buildState(text: string) {
  const state = text.split(",").map((t, i) =>
    Object.fromEntries([
      ["name", BASE_VALUES[i][0]],
      ["enabled", !!+t],
      ["use", BASE_VALUES[i][1]],
    ]),
  );

  return state;
}
