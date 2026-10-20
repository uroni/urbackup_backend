/**
 * Automatic archival rules as stored in the "archive" setting, e.g.
 * "every_c0=86400&every_unit_c0=d&for_c0=604800&for_unit_c0=w&..."
 */
export interface ArchiveRule {
  every: number; // In `everyUnit`
  everyUnit: TimeUnit;
  for: number; // In `forUnit`, ignored for "forever"
  forUnit: TimeUnit | "i";
  backupType: string;
  window: string;
  letters: string;
  uuid?: string;
  // Unix time of the next archival and seconds until then, of the rules of
  // a client. Only sent by the server, not saved.
  next?: number;
  timeLeft?: number;
}

export type TimeUnit = "h" | "d" | "w" | "m" | "y";

// Same factors as the old web interface (a month is 4.345 weeks)
const UNIT_SECONDS: Record<TimeUnit, number> = {
  h: 60 * 60,
  d: 60 * 60 * 24,
  w: 60 * 60 * 24 * 7,
  m: 60 * 60 * 24 * 7 * 4.345,
  y: 60 * 60 * 24 * 7 * 4.345 * 12,
};

export function toSeconds(value: number, unit: TimeUnit | "i") {
  return unit === "i" ? -1 : value * UNIT_SECONDS[unit];
}

export function fromSeconds(seconds: number, unit: TimeUnit | "i") {
  if (unit === "i") {
    return 0;
  }

  // Stored in whole seconds, so months/years come back as e.g. 0.99999...
  return Math.round((seconds / UNIT_SECONDS[unit]) * 100) / 100;
}

/**
 * Rules of the setting value with the index prefixes `prefixes`: "c" for the
 * rules of a client, "d" and "g" for the default and group rules
 */
export function parseArchiveRules(value: string, prefixes: string[]) {
  const params = new URLSearchParams(value);
  const rules: ArchiveRule[] = [];

  for (const prefix of prefixes) {
    for (let i = 0; params.has(`every_${prefix}${i}`); i++) {
      const get = (k: string) => params.get(`${k}_${prefix}${i}`) ?? "";
      const everyUnit = get("every_unit") as TimeUnit;
      const forUnit = get("for_unit") as TimeUnit | "i";

      rules.push({
        every: fromSeconds(Number(get("every")), everyUnit),
        everyUnit,
        for: fromSeconds(Number(get("for")), forUnit),
        forUnit,
        backupType: get("backup_type"),
        window: get("window"),
        letters: get("letters"),
        uuid: params.get(`uuid_${prefix}${i}`) ?? undefined,
        ...(params.has(`next_archival_${prefix}${i}`) && {
          next: Number(get("next_archival")),
          timeLeft: Number(get("timeleft")),
        }),
      });
    }
  }

  return rules;
}

/**
 * Setting value of the rules of a client, or with the prefix "d" of the
 * general settings
 */
export function serializeArchiveRules(rules: ArchiveRule[], prefix = "c") {
  const params = new URLSearchParams();

  rules.forEach((r, n) => {
    const i = `${prefix}${n}`;
    params.set(`every_${i}`, String(toSeconds(r.every, r.everyUnit)));
    params.set(`every_unit_${i}`, r.everyUnit);
    params.set(`for_${i}`, String(toSeconds(r.for, r.forUnit)));
    params.set(`for_unit_${i}`, r.forUnit);
    params.set(`backup_type_${i}`, r.backupType);
    params.set(`window_${i}`, r.window);
    params.set(`letters_${i}`, r.letters);
    // The server sets the ids of new rules
    if (r.uuid && r.uuid.length > 10) {
      params.set(`uuid_${i}`, r.uuid);
    }
  });

  return params.toString();
}

// Same check as the old web interface: hour;day of month;month;day of week
export const ARCHIVE_WINDOW_PATTERN =
  /^((([0-9]+,?)+)|\*);((([0-9]+,?)+)|\*);((([0-9]+,?)+)|\*);((([0-9]+,?)+)|\*)$/i;
