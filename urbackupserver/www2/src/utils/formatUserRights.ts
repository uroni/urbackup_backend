import type { UserRight } from "../api/urbackupserver";

export function formatUserRights(rights: UserRight[]) {
  const transformed = rights
    .map((r, i) =>
      Object.entries(r).map(([key, value]) => addIdxToEntry(i, key, value)),
    )
    .map((e) => new URLSearchParams(e).toLocaleString());

  const formattedIdx = `idx=${Array.from(rights, (_, i) => i).join(",")}`;

  return [transformed, formattedIdx].join("&");
}

function addIdxToEntry(i: number, key: string, value: string) {
  return [`${i}_${key}`, value];
}
