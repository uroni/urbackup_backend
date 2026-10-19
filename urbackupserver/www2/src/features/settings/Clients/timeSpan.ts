/**
 * Multiply the values of a time span setting like "10@1-5/8-17;50" by `m`.
 * Port of `multiplyTimeSpan` of the old web interface. With `allowPercent`
 * a value of "P%" is stored as -P-1.
 */
export function multiplyTimeSpan(ts: string, m: number, allowPercent: boolean) {
  return ts
    .split(";")
    .map((timespan) => {
      const idx = timespan.indexOf("@");
      const window = idx === -1 ? "" : timespan.substring(idx);
      const value = idx === -1 ? timespan : timespan.substring(0, idx);
      const idxPc = value.indexOf("%");

      if (idxPc !== -1 && allowPercent) {
        return `${parseFloat(value.substring(0, idxPc)) * -1 - 1}${window}`;
      }

      const d = parseFloat(value);

      if (d < -1 && m < 1 && allowPercent) {
        return `${d * -1 - 1}%${window}`;
      }

      return `${d * m}${window}`;
    })
    .join(";");
}

/**
 * Converts a speed setting (bytes per second) to `bytesPerUnit` units
 * and back. "-" means no limit.
 */
export function speedFormat(bytesPerUnit: number) {
  return {
    ui: (v: string) =>
      v === "0" || v === "-1"
        ? "-"
        : multiplyTimeSpan(v, 1 / bytesPerUnit, true),
    api: (v: string) =>
      v === "-" || v === "" ? "0" : multiplyTimeSpan(v, bytesPerUnit, true),
  };
}

export const MBIT_PER_S = (1024 * 1024) / 8;
export const KBIT_PER_S = 1024 / 8;

// Same check as the old web interface
export const SPEED_PATTERN =
  /^-$|^([\d.]*[%]?(@([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*)?[;]?)*$/i;
