import { Button, Link } from "@fluentui/react-components";

// Filled in by build/replace_versions.py for releases
const version = document
  .querySelector('meta[name="urbackup-version"]')
  ?.getAttribute("content");
// Without a release build the placeholder is still there
const shownVersion = version?.startsWith("$") ? "" : version;

const translators = [
  ["German", "Martin Raiber, Ettore Atalan"],
  ["Spanish", "Luis Miguel Muñoz"],
  ["Turkish", "Mehmet Binici"],
  ["Finnish", "Jussi Bergström"],
  ["French", "mehdincd, Charles Peltier"],
  ["Italian", "Samuele, Paolo, Marco Longo"],
  ["Dutch", "buzzertnl, Pimmetje"],
  ["Portuguese (Brazil)", "Artur Corumba, Juan Pablo Kerber"],
  ["Portuguese", "J. Almeida"],
  ["Norwegian", "matsr"],
  ["Slovak", "janda"],
  ["Swedish", "Jonas Aaslund"],
  ["Czech", "Ales Hermann"],
  ["Russian", "Artem Alabin"],
  ["Romanian", "Olivian Daniel Tofan"],
  ["Ukrainian", "Ihor Maydanovich"],
  ["Farsi", "osiengine group"],
  ["Traditional and Simplified Chinese", "Zhengyu Ren, Johnny Xing, 五月鸢飞"],
  [
    "Polish",
    "Czeslaw Mruk, Jarosław Gorzelnik, Krzysztof Pałka, Łukasz Milata, Maciej Dyczko, Thomas Pancherz, Wojciech Staszewski",
  ],
];

const libraries = [
  ["The Crypto++ Library", "https://www.cryptopp.com/"],
  ["The SQLite embedded database", "https://sqlite.org/"],
  ["cURL Library", "https://curl.se/"],
  ["Symas Lightning Memory-Mapped Database", "https://www.symas.com/mdb"],
  ["FastCGI C++ Library", "http://www.nongnu.org/fastcgi/"],
  [
    "Mersenne Twister with improved initialization",
    "http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/MT2002/emt19937ar.html",
  ],
  ["MD5 implementation", "http://userpages.umbc.edu/~mabzug1/cs/md5/md5-cc/"],
  [
    "MiniZ: ZIP archive reading/writing library",
    "https://github.com/richgel999/miniz",
  ],
  [
    "Templatized C++ Command Line Parser Library",
    "http://tclap.sourceforge.net/",
  ],
  ["React", "https://react.dev/"],
  ["Fluent UI React", "https://react.fluentui.dev/"],
  ["TanStack Query", "https://tanstack.com/query"],
  ["React Router", "https://reactrouter.com/"],
  ["Zod", "https://zod.dev/"],
  ["crypto-js", "https://github.com/brix/crypto-js"],
];

export function AboutPage() {
  return (
    <div className="flow" style={{ maxWidth: "80ch" }}>
      <h2>About UrBackup</h2>
      <p>
        <strong>UrBackup Server {shownVersion}</strong>
      </p>

      <h3>Authors</h3>
      <ul>
        <li>
          <Link href="https://github.com/uroni/">Martin Raiber</Link>
        </li>
        <li>
          <Link href="https://github.com/mambojuice/">mambojuice</Link>
        </li>
      </ul>

      <h3>Translators</h3>
      <ul>
        {translators.map(([language, names]) => (
          <li key={language}>
            {names} ({language})
          </li>
        ))}
      </ul>

      <h3>Support UrBackup</h3>
      <p>
        A lot of work has gone into UrBackup. If you like it and want to support
        the ongoing development please consider donating.
      </p>
      <form
        action="https://www.paypal.com/cgi-bin/webscr"
        method="post"
        target="_blank"
      >
        <input type="hidden" name="cmd" value="_s-xclick" />
        <input type="hidden" name="hosted_button_id" value="GFTJQ6MRQTBXA" />
        <Button type="submit">Donate via PayPal</Button>
      </form>
      <p>
        Contribution of build server or testing infrastructure is welcome as
        well. The most appreciated contribution would be your time in form of
        help.
      </p>

      <h3>UrBackup is using the following libraries/code</h3>
      <ul>
        {libraries.map(([name, href]) => (
          <li key={name}>
            <Link href={href}>{name}</Link>
          </li>
        ))}
      </ul>

      <h3>License</h3>
      <p>
        UrBackup is licensed as{" "}
        <Link href="https://www.gnu.org/licenses/agpl-3.0.html">
          <strong>AGPLv3+</strong>
        </Link>
        . See the server-license.txt in your software distribution for the full
        license text of UrBackup and the licenses of used third-party software.
        The source code of this server software instance is available for
        example at{" "}
        <Link href="https://github.com/uroni/urbackup_backend">
          https://github.com/uroni/urbackup_backend
        </Link>
        .
      </p>
    </div>
  );
}
