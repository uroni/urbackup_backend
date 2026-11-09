import { describe, expect, it } from "vitest";

import { fromServerUrl, isValidServerUrl, toServerUrl } from "./internetServer";

describe("internet server URL", () => {
  it.each([
    ["", "55415", ""],
    ["backup.example.com", "55415", "urbackup://backup.example.com"],
    ["backup.example.com", "1234", "urbackup://backup.example.com:1234"],
    ["wss://example.com/socket", "55415", "wss://example.com/socket"],
  ])("shows %s:%s as %s", (server, port, url) =>
    expect(toServerUrl(server, port)).toBe(url),
  );

  it.each([
    ["urbackup://", { server: "", port: "55415" }],
    [
      "urbackup://a.example.com:1234",
      { server: "a.example.com", port: "1234" },
    ],
    ["a.example.com", { server: "a.example.com", port: "55415" }],
    [
      "ws://example.com:80/socket",
      { server: "ws://example.com:80/socket", port: "55415" },
    ],
    ["urbackup://a:1;urbackup://b", { server: "a;b", port: "1" }],
  ])("stores %s", (url, stored) => expect(fromServerUrl(url)).toEqual(stored));

  it("keeps the stored value", () => {
    const stored = fromServerUrl(toServerUrl("a.example.com", "1234"));
    expect(stored).toEqual({ server: "a.example.com", port: "1234" });
  });

  it.each(["", "urbackup://a.example.com:1", "a.b;wss://c/d", "10.0.0.1"])(
    "accepts %s",
    (url) => expect(isValidServerUrl(url)).toBe(true),
  );

  it.each(["http://a.example.com", "a b", "urbackup://"])("rejects %s", (url) =>
    expect(isValidServerUrl(url)).toBe(false),
  );
});
