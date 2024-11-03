import { i18n } from "@lingui/core";

function determine_date_format() {
  //Create a known date string
  const y = new Date(2013, 9, 25);
  let lds;
  if (navigator.languages && navigator.languages.length)
    lds = y.toLocaleDateString(navigator.languages[0]);
  else lds = y.toLocaleDateString();

  //search for the position of the year, day, and month
  const yPosi = lds.search("2013");
  let dPosi = lds.search("25");
  let mPosi = lds.search("10");

  if (dPosi == -1) {
    dPosi = lds.search("24");
    if (dPosi == -1) {
      dPosi = lds.search("26");
    }
  }

  // try to determine date separator
  let dateSeperator = "/";
  const pointPos = lds.indexOf(".");
  if (pointPos != -1) dateSeperator = ".";
  const dashPos = lds.indexOf("-");
  if (dashPos != -1) dateSeperator = "-";

  //Sometimes the month is displayed by the month name so guess where it is
  if (mPosi == -1) {
    mPosi = lds.search("9");
    if (mPosi == -1) {
      //if the year and day are not first then maybe month is first
      if (yPosi != 0 && dPosi != 0) {
        mPosi = 0;
      }
      //if year and day are not last then maybe month is last
      else if (yPosi + 4 < lds.length && dPosi + 2 < lds.length) {
        mPosi = Infinity;
      }
      //otherwist is in the middle
      else if (yPosi < dPosi) {
        mPosi = (dPosi - yPosi) / 2 + yPosi;
      } else if (dPosi < yPosi) {
        mPosi = (yPosi - dPosi) / 2 + dPosi;
      }
    }
  }

  let formatString = "";

  const order = [yPosi, dPosi, mPosi];
  order.sort(function (a, b) {
    return a - b;
  });

  for (let i = 0; i < order.length; i++) {
    if (i > 0) formatString += dateSeperator;

    if (order[i] == yPosi) {
      if (i == 0) formatString += "YYYY";
      else formatString += "YY";
    } else if (order[i] == dPosi) {
      formatString += "DD";
    } else if (order[i] == mPosi) {
      formatString += "MM";
    }
  }

  return formatString;
}

const dateFormatString = determine_date_format();

export function format_date(d: Date) {
  let wt: number | string = d.getDate();
  if (wt < 10) wt = "0" + wt;
  let m: number | string = d.getMonth();
  ++m;
  if (m < 10) m = "0" + m;
  let j: number | string = d.getFullYear();
  j -= 2000;
  if (j < 10) j = "0" + j;

  let h: number | string = d.getHours();
  if (h < 10) h = "0" + h;

  let min: number | string = d.getMinutes();
  if (min < 10) min = "0" + min;

  return (
    dateFormatString
      .replace("YYYY", "" + d.getFullYear())
      .replace("YY", "" + j)
      .replace("MM", "" + m)
      .replace("DD", "" + wt) +
    " " +
    h +
    ":" +
    min
  );
}

export function format_unix_timestamp(ts: number) {
  return format_date(new Date(ts * 1000));
}

export function format_size(s: number) {
  let suffix = "bytes";
  if (s > 1024) {
    s /= 1024.0;
    suffix = "KiB";
  }
  if (s > 1024) {
    s /= 1024.0;
    suffix = "MiB";
  }
  if (s > 1024) {
    s /= 1024.0;
    suffix = "GiB";
  }
  if (s > 1024) {
    s /= 1024.0;
    suffix = "TiB";
  }

  s *= 100;
  s = Math.round(s);
  s /= 100.0;
  return s + " " + suffix;
}

export function format_bits(bits: number) {
  let suffix = "bits";
  if (bits > 1000) {
    bits /= 1000.0;
    suffix = "Kbit";
  }
  if (bits > 1000) {
    bits /= 1000.0;
    suffix = "Mbit";
  }
  if (bits > 1000) {
    bits /= 1000.0;
    suffix = "Gbit";
  }
  if (bits > 1000) {
    bits /= 1000.0;
    suffix = "Tbit";
  }

  bits *= 100;
  bits = Math.round(bits);
  bits /= 100.0;

  return {
    value: bits,
    suffix,
  };
}

export function format_size_bits(s: number) {
  const { value, suffix } = format_bits(s);

  const suffix_trans = i18n._(suffix);
  return value + " " + (suffix_trans != null ? suffix_trans : suffix);
}

/**
 * Convert speed from bytes/ms to bits/s
 * @param bpms Speed in bytes per ms
 * @returns Speed in bits per s
 */
export function format_speed_bpms_to_bps(bpms: number) {
  const speedInBps = bpms * 8000;

  const { value, suffix } = format_bits(speedInBps);

  const speedSuffix = `${suffix}/s`;

  const suffix_trans = i18n._(speedSuffix);
  return value + " " + (suffix_trans != null ? suffix_trans : suffix);
}

/**
 * Formats a date object into - "YYYY-MM-DD, hh:mm"
 * @param datetime {Date}
 * @returns string
 */
export function formatDatetime(datetime: number) {
  const date = new Date(datetime * 1000);

  const formatter = new Intl.DateTimeFormat("en-US", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  });

  return formatter.format(date);
}

export function formatDuration(durationInSeconds: number) {
  const hours = Math.floor(durationInSeconds / 60 / 60);

  const minutes = Math.floor(durationInSeconds / 60) - hours * 60;

  if (hours > 0) {
    return `${hours}h ${minutes}m`;
  }

  return `${minutes}m`;
}
