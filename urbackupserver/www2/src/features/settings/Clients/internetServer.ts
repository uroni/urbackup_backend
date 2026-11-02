const DEFAULT_PORT = "55415";

/**
 * Server URL shown for the stored "internet_server" and "internet_server_port",
 * like the old web interface: a plain host name gets the urbackup:// scheme
 */
export function toServerUrl(server: string, port: string) {
  // Unlike the old web interface, which showed "urbackup://" and then
  // couldn't save it
  if (server === "" || /^(ws|wss|urbackup):\/\//.test(server)) {
    return server;
  }

  return port === DEFAULT_PORT || port === ""
    ? `urbackup://${server}`
    : `urbackup://${server}:${port}`;
}

/**
 * Stored "internet_server" and "internet_server_port" of the shown server
 * URLs (separated by ";"). The port of the last urbackup:// URL with one is
 * stored for all of them, like in the old web interface.
 */
export function fromServerUrl(url: string) {
  let port = DEFAULT_PORT;

  const servers = url.split(";").map((s) => {
    const urbackup = s.match(/^urbackup:\/\/([^:/]*)(?::(\d+))?/);

    if (!urbackup) {
      return s;
    }

    port = urbackup[2] ?? port;
    return urbackup[1];
  });

  return { server: servers.join(";"), port };
}

// Same check as the old web interface, for each of the servers
const SERVER_PATTERN =
  /^$|^[\w-]+(\.[\w-]*)*$|^(ws|wss|urbackup):\/\/[\w-]+[\w.,@?^=%&:/~+#-]*$/i;

export function isValidServerUrl(url: string) {
  return url.split(";").every((s) => SERVER_PATTERN.test(s));
}
