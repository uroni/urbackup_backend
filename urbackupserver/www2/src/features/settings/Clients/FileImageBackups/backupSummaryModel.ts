// Describes the interval and min/max number of backups settings in plain text
// and warns about combinations the server cannot keep.

const MAX_SLOTS = 40000;
const MAX_BACKUPS = 4000;
// Backups looked at while running the schedule, to keep the page responsive
const MAX_WORK = 20000000;

export const SECONDS_PER_HOUR = 60 * 60;
export const SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;

const KIND_TEXTS = {
  file: {
    what_full: "full file backups",
    what_incr: "incremental file backups",
    sched_both:
      "A full file backup runs about every {full} and an incremental file backup about every {incr} in between.",
    sched_all_full:
      "Every scheduled file backup is a full file backup, about every {incr}.",
    sched_incr:
      "An incremental file backup runs about every {incr}. Full file backups are not scheduled, only the first file backup is a full one.",
    sched_full:
      "A full file backup runs about every {full}. Incremental file backups are disabled.",
    sched_none: "No file backups are scheduled.",
    keep: "The server keeps at most {max_full} full and {max_incr} incremental file backups. Each of them can be restored on its own.",
    warn_full_le_incr:
      "The interval for full file backups ({full}) is not longer than the interval for incremental file backups ({incr}). No incremental file backups are made and a full file backup runs every {incr}.",
  },
  image: {
    what_full: "full image backups",
    what_incr: "incremental image backups",
    sched_both:
      "A full image backup runs about every {full} and an incremental image backup about every {incr} in between.",
    sched_all_full:
      "Every scheduled image backup is a full image backup, about every {incr}.",
    sched_incr:
      "An incremental image backup runs about every {incr}. Full image backups are not scheduled, only the first image backup of a volume is a full one.",
    sched_full:
      "A full image backup runs about every {full}. Incremental image backups are disabled.",
    sched_none: "No image backups are scheduled.",
    keep: "The server keeps at most {max_full} full and {max_incr} incremental image backups per volume. An incremental image backup can only be restored together with the image backups it is based on, so those are not deleted before it.",
    warn_full_le_incr:
      "The interval for full image backups ({full}) is not longer than the interval for incremental image backups ({incr}). No incremental image backups are made and a full image backup runs every {incr}.",
  },
};

const TEXTS = {
  sched_varies:
    "The intervals depend on the time of day or week, so the schedule is not described here.",
  sched_note:
    "Backups only start while the client is online and inside the backup window, so they can run later than that.",
  reach_both:
    "If every scheduled backup runs, every backup of the last {fine} can be restored. Further back only some of them are left, the oldest one is about {all} old.",
  reach_one:
    "If every scheduled backup runs, the backups reach back about {all}.",
  space:
    "If the backup storage runs out of space, the oldest backups are deleted earlier, down to {min_full} full and {min_incr} incremental ones.",
  warn_min_max:
    "The minimal number of {what} ({min}) is larger than the maximal number ({max}). Never more than {max} are kept, and none of them can be deleted if the backup storage runs out of space.",
  warn_max_zero:
    "The maximal number of {what} is 0, so they are deleted again by the next cleanup.",
  incr_only_image:
    "All incremental image backups are based, directly or through earlier ones, on the first full image backup, so that one is never deleted.",
  warn_newest:
    "With these settings the cleanup deletes the newest incremental image backups instead of the oldest ones. Incremental image backups are based on each other here, and one that others are based on is only deleted together with them. Keep at least {n} incremental image backups or base them on the last full image backup (incremental image style).",
  warn_full_max:
    "The maximal number of full image backups ({max}) cannot always be kept. A full image backup is only deleted together with the incremental image backups based on it, and the minimal number of incremental image backups ({min_incr}) prevents that. Expect up to {n} full image backups.",
  warn_incr_max:
    "The maximal number of incremental image backups ({max}) cannot always be kept, because an incremental image backup that others are based on is only deleted together with them. Expect up to {n} incremental image backups.",
  hour: "1 hour",
  hours: "{n} hours",
  day: "1 day",
  days: "{n} days",
  months: "{n} months",
  years: "{n} years",
};

function msg(text: string, params?: Record<string, string | number>) {
  let ret = text;

  for (const [key, value] of Object.entries(params ?? {})) {
    ret = ret.split(`{${key}}`).join(String(value));
  }

  return ret;
}

export function duration(seconds: number) {
  const round1 = (v: number) => Math.round(v * 10) / 10;

  const hours = seconds / SECONDS_PER_HOUR;
  if (hours < 48) {
    const n = round1(hours);
    return n == 1 ? TEXTS.hour : msg(TEXTS.hours, { n });
  }

  const days = hours / 24;
  if (days < 60) {
    const n = round1(days);
    return n == 1 ? TEXTS.day : msg(TEXTS.days, { n });
  }

  if (days < 730) {
    return msg(TEXTS.months, { n: Math.round(days / 30.44) });
  }

  return msg(TEXTS.years, { n: round1(days / 365.25) });
}

/**
 * Seconds, -1 if disabled, "varies" for intervals with time windows and null
 * if there is no usable number.
 */
export type Interval = number | "varies" | null;

/**
 * Interval as entered in the settings: number in units of unitSeconds.
 */
export function parseInterval(
  text: string | number | undefined,
  disabled: boolean,
  unitSeconds: number,
): Interval {
  if (disabled) {
    return -1;
  }

  const trimmed = String(text ?? "").trim();

  if (trimmed.includes("@") || trimmed.includes(";")) {
    return "varies";
  }

  if (!/^-?[\d.]+$/.test(trimmed)) {
    return null;
  }

  const val = parseFloat(trimmed);

  if (isNaN(val) || val == 0) {
    return null;
  }

  if (val < 0) {
    return -1;
  }

  return val * unitSeconds;
}

export function parseCount(text: string | number | undefined) {
  const trimmed = String(text ?? "").trim();

  if (!/^\d+$/.test(trimmed)) {
    return null;
  }

  return parseInt(trimmed, 10);
}

interface SimulationConfig {
  incr: number; // seconds, -1 if disabled
  full: number; // seconds, -1 if disabled
  max_full: number;
  max_incr: number;
  min_incr: number;
  image: boolean;
  to_last: boolean; // incremental image backups based on the last image backup
}

interface SimulationResult {
  fulls_peak: number;
  incr_peak: number;
  fine: number; // seconds
  all: number; // seconds
  cycle: number; // seconds
  per_cycle: number;
  newest_lost: boolean;
}

interface SimulatedBackup {
  t: number; // slot
  full: boolean;
  ref: number; // slot of the backup it is based on or -1
}

/**
 * Runs the backup schedule and the cleanup of the server
 * (ServerCleanupThread) for one client and volume until the number of
 * backups has settled. Returns null if that would take too long.
 */
export function simulate(cfg: SimulationConfig): SimulationResult | null {
  if (cfg.incr <= 0 && cfg.full <= 0) {
    return null;
  }

  const step = cfg.incr > 0 ? cfg.incr : cfg.full;
  // Every cycleSlots-th backup is a full one; 0: only the first one
  let cycleSlots = 1;
  if (cfg.incr > 0 && cfg.full > 0) {
    cycleSlots = Math.max(1, Math.ceil(cfg.full / cfg.incr));
  } else if (cfg.incr > 0) {
    cycleSlots = 0;
  }

  const perCycle = cycleSlots > 0 ? cycleSlots - 1 : 1;
  const cycles =
    cfg.max_full +
    Math.ceil((cfg.max_incr + cfg.min_incr) / Math.max(perCycle, 1)) +
    6;
  let slots = cycles * Math.max(cycleSlots, 1);
  let measureSlots = 2 * Math.max(cycleSlots, 1) + 2;
  if (cycleSlots == 0) {
    slots = 2 * (cfg.max_incr + cfg.min_incr) + 50;
    measureSlots = cfg.max_incr + cfg.min_incr + 10;
  }

  if (slots > MAX_SLOTS || cfg.max_full + cfg.max_incr > MAX_BACKUPS) {
    return null;
  }

  // Oldest first
  const backups: SimulatedBackup[] = [];
  let work = 0;

  const count = (full: boolean) => {
    work += backups.length;
    return backups.filter((b) => b.full == full).length;
  };

  const oldest = (full: boolean, notit: number[]) => {
    work += backups.length;
    return backups.filter((b) => b.full == full && !notit.includes(b.t));
  };

  const remove = (backup: SimulatedBackup) => {
    backups.splice(backups.indexOf(backup), 1);
  };

  // ServerCleanupThread::removeImage with remove_references
  const removeImage = (
    backup: SimulatedBackup,
    delIncrInStack: number,
  ): boolean => {
    work += backups.length;
    if (work > MAX_WORK) {
      return false;
    }

    const refs = backups.filter((b) => !b.full && b.ref == backup.t);

    for (const ref of refs) {
      if (count(false) - delIncrInStack - cfg.min_incr <= 0) {
        return false;
      }
      if (!removeImage(ref, delIncrInStack + 1)) {
        return false;
      }
    }

    remove(backup);
    return true;
  };

  // ServerCleanupThread::cleanup_one_imagebackup_client
  const cleanupOneImage = () => {
    let notit: number[] = [];
    let cand = oldest(true, notit);
    while (cand.length > cfg.max_full) {
      if (removeImage(cand[0], 0)) {
        return true;
      }
      notit.push(cand[0].t);
      cand = oldest(true, notit);
    }

    notit = [];
    cand = oldest(false, notit);
    while (cand.length > cfg.max_incr) {
      if (removeImage(cand[0], 1)) {
        return true;
      }
      notit.push(cand[0].t);
      cand = oldest(false, notit);
    }
    return false;
  };

  // ServerCleanupThread::cleanup_one_filebackup_client
  const cleanupFiles = () => {
    let cand = oldest(true, []);
    while (
      cand.length > cfg.max_full &&
      !(
        cand.length == 1 &&
        cfg.max_incr > 0 &&
        cfg.incr > 0 &&
        count(false) == 0
      )
    ) {
      remove(cand[0]);
      cand = oldest(true, []);
    }

    cand = oldest(false, []);
    while (cand.length > cfg.max_incr) {
      remove(cand[0]);
      cand = oldest(false, []);
    }
  };

  const ret: SimulationResult = {
    fulls_peak: 0,
    incr_peak: 0,
    fine: -1,
    all: -1,
    cycle: Math.max(cycleSlots, 1) * step,
    per_cycle: perCycle,
    newest_lost: false,
  };

  for (let t = 0; t < slots; ++t) {
    const fulls = oldest(true, []);
    const lastFull = fulls.length > 0 ? fulls[fulls.length - 1].t : -1;
    // An incremental image backup needs a full one, an incremental file backup any backup
    const needFull = cfg.image ? lastFull == -1 : backups.length == 0;

    if (
      needFull ||
      (cycleSlots > 0 && (lastFull == -1 || t - lastFull >= cycleSlots))
    ) {
      backups.push({ t, full: true, ref: -1 });
    } else {
      backups.push({
        t,
        full: false,
        ref: cfg.to_last ? backups[backups.length - 1].t : lastFull,
      });
    }

    if (cfg.image) {
      while (cleanupOneImage()) {
        // Until nothing is left to remove
      }
    } else {
      cleanupFiles();
    }

    if (work > MAX_WORK) {
      return null;
    }

    if (t < slots - measureSlots) {
      continue;
    }

    if (backups.length == 0 || backups[backups.length - 1].t != t) {
      ret.newest_lost = true;
    }

    ret.fulls_peak = Math.max(ret.fulls_peak, count(true));
    ret.incr_peak = Math.max(ret.incr_peak, count(false));

    const all = backups.length > 0 ? t - backups[0].t : 0;
    let fine = 0;
    for (let i = backups.length - 1; i >= 0 && backups[i].t == t - fine; --i) {
      ++fine;
    }
    fine = Math.max(fine - 1, 0);

    if (ret.all == -1 || all < ret.all) {
      ret.all = all;
    }
    if (ret.fine == -1 || fine < ret.fine) {
      ret.fine = fine;
    }
  }

  ret.fine *= step;
  ret.all *= step;
  return ret;
}

export interface BackupSummarySettings {
  image: boolean;
  incr: Interval;
  full: Interval;
  max_full: number | null;
  min_full: number | null;
  max_incr: number | null;
  min_incr: number | null;
  // The incremental image styles in use (true: based on the last image backup)
  to_last: boolean[];
}

export interface BackupSummary {
  info: string[];
  warn: string[];
}

export function describe(settings: BackupSummarySettings): BackupSummary {
  const kind = settings.image ? KIND_TEXTS.image : KIND_TEXTS.file;
  const ret: BackupSummary = { info: [], warn: [] };

  const { incr, full } = settings;
  let hasSchedule = false;

  if (incr == "varies" || full == "varies") {
    ret.info.push(TEXTS.sched_varies);
  } else if (incr !== null && full !== null) {
    hasSchedule = incr > 0 || full > 0;
    if (incr > 0 && full > 0 && full <= incr) {
      ret.info.push(msg(kind.sched_all_full, { incr: duration(incr) }));
      ret.warn.push(
        msg(kind.warn_full_le_incr, {
          full: duration(full),
          incr: duration(incr),
        }),
      );
    } else if (incr > 0 && full > 0) {
      ret.info.push(
        msg(kind.sched_both, {
          full: duration(Math.ceil(full / incr) * incr),
          incr: duration(incr),
        }),
      );
    } else if (incr > 0) {
      ret.info.push(msg(kind.sched_incr, { incr: duration(incr) }));
    } else if (full > 0) {
      ret.info.push(msg(kind.sched_full, { full: duration(full) }));
    } else {
      ret.info.push(kind.sched_none);
    }

    if (hasSchedule) {
      ret.info.push(TEXTS.sched_note);
    }
  }

  const { max_full, min_full, max_incr, min_incr } = settings;

  if (
    max_full === null ||
    min_full === null ||
    max_incr === null ||
    min_incr === null
  ) {
    return ret;
  }

  ret.info.push(msg(kind.keep, { max_full, max_incr }));

  // Only numbers from here on: "varies" and null leave hasSchedule false
  const incrSec = typeof incr == "number" ? incr : -1;
  const fullSec = typeof full == "number" ? full : -1;

  const fullUsed = hasSchedule && (fullSec > 0 || incrSec > 0);
  const incrUsed =
    hasSchedule && incrSec > 0 && !(fullSec > 0 && fullSec <= incrSec);

  if (min_full > max_full) {
    ret.warn.push(
      msg(TEXTS.warn_min_max, {
        what: kind.what_full,
        min: min_full,
        max: max_full,
      }),
    );
  }
  if (min_incr > max_incr) {
    ret.warn.push(
      msg(TEXTS.warn_min_max, {
        what: kind.what_incr,
        min: min_incr,
        max: max_incr,
      }),
    );
  }
  if (max_full == 0 && fullUsed) {
    ret.warn.push(msg(TEXTS.warn_max_zero, { what: kind.what_full }));
  }
  if (max_incr == 0 && incrUsed) {
    ret.warn.push(msg(TEXTS.warn_max_zero, { what: kind.what_incr }));
  }

  let sim: SimulationResult | null = null;
  if (hasSchedule && settings.image && fullSec <= 0) {
    ret.info.push(TEXTS.incr_only_image);
  } else if (hasSchedule) {
    const styles = settings.image ? settings.to_last : [false];
    for (const toLast of styles) {
      const curr = simulate({
        incr: incrSec,
        full: fullSec,
        max_full,
        max_incr,
        min_incr,
        image: settings.image,
        to_last: toLast,
      });

      if (curr === null) {
        sim = null;
        break;
      }

      if (sim === null) {
        sim = curr;
      } else {
        sim.fulls_peak = Math.max(sim.fulls_peak, curr.fulls_peak);
        sim.incr_peak = Math.max(sim.incr_peak, curr.incr_peak);
        sim.fine = Math.min(sim.fine, curr.fine);
        sim.all = Math.min(sim.all, curr.all);
        sim.newest_lost = sim.newest_lost || curr.newest_lost;
      }
    }
  }

  if (sim !== null) {
    const step = incrSec > 0 ? incrSec : fullSec;
    // Without scheduled full backups the first one stays, however old it gets
    if (fullSec <= 0) {
      sim.all = sim.fine;
    }

    if (sim.all > 0 && sim.fine > 0 && sim.fine + step < sim.all) {
      ret.info.push(
        msg(TEXTS.reach_both, {
          fine: duration(sim.fine),
          all: duration(sim.all),
        }),
      );
    } else if (sim.all > 0) {
      ret.info.push(msg(TEXTS.reach_one, { all: duration(sim.all) }));
    }

    if (settings.image && sim.newest_lost && incrUsed && max_incr > 0) {
      ret.warn.push(msg(TEXTS.warn_newest, { n: sim.per_cycle }));
    }

    if (settings.image && sim.fulls_peak > max_full && max_full > 0) {
      ret.warn.push(
        msg(TEXTS.warn_full_max, {
          max: max_full,
          min_incr,
          n: sim.fulls_peak,
        }),
      );
    }
    if (
      settings.image &&
      sim.incr_peak > max_incr &&
      max_incr > 0 &&
      !sim.newest_lost
    ) {
      ret.warn.push(
        msg(TEXTS.warn_incr_max, { max: max_incr, n: sim.incr_peak }),
      );
    }
  }

  if (
    min_full <= max_full &&
    min_incr <= max_incr &&
    (min_full < max_full || min_incr < max_incr)
  ) {
    ret.info.push(msg(TEXTS.space, { min_full, min_incr }));
  }

  return ret;
}
