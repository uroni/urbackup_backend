/**
 * `version` ("2.5.37 Rev. ...") as a number like the server's curr_version_num
 * (2005003700), or undefined without a release build
 */
export function serverVersionNumber(version: string | null | undefined) {
  // Without a release build the version is the "$version_full$" placeholder
  const m = version?.match(/^(\d+)\.(\d+)\.(\d+)/);

  if (!m) {
    return undefined;
  }

  return +m[1] * 1_000_000_000 + +m[2] * 1_000_000 + +m[3] * 100;
}
