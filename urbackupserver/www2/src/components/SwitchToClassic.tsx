import { Link, tokens } from "@fluentui/react-components";
import { ArrowLeftRegular } from "@fluentui/react-icons";
import { Trans } from "@lingui/macro";

// The classic interface redirects to this one while the cookie is set
// (see www/index.htm), so it has to be removed before going back.
function switchToClassic() {
  const path = location.pathname.replace(/www2\/[^/]*$/, "");
  document.cookie = `urbackup_www2=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=${path}`;
  location.href = "../";
}

export const SwitchToClassic = () => {
  return (
    <Link
      as="button"
      appearance="subtle"
      onClick={switchToClassic}
      style={{
        display: "inline-flex",
        alignItems: "center",
        gap: tokens.spacingHorizontalXS,
        fontSize: tokens.fontSizeBase200,
        color: tokens.colorNeutralForeground3,
      }}
    >
      <ArrowLeftRegular />
      <Trans>Classic interface</Trans>
    </Link>
  );
};

export default SwitchToClassic;
