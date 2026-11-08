import { OpenRegular } from "@fluentui/react-icons";

import styles from "./NewTabLink.module.css";

export function NewTabLink(
  props: React.AnchorHTMLAttributes<HTMLAnchorElement>,
) {
  return (
    <a target="_blank" rel="noreferrer" {...props}>
      {props.children}
      <span className="visually-hidden"> (opens in a new tab)</span>
      <OpenRegular className={styles.icon} />
    </a>
  );
}
