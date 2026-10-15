import { MessageBar, MessageBarBody } from "@fluentui/react-components";

import { type BackupSummary as Summary } from "./backupSummaryModel";
import styles from "./FileImageBackups.module.css";

/**
 * Shows what the interval and number of backups settings add up to, and
 * warnings for values the server does not keep to.
 */
export function BackupSummary({ summary }: { summary: Summary }) {
  if (!summary.info.length && !summary.warn.length) {
    return null;
  }

  return (
    <div className={styles["backup-summary"]} aria-live="polite">
      {summary.info.length > 0 && (
        <MessageBar intent="info" layout="multiline">
          <MessageBarBody>
            {summary.info.map((text) => (
              <p key={text}>{text}</p>
            ))}
          </MessageBarBody>
        </MessageBar>
      )}
      {summary.warn.map((text) => (
        <MessageBar key={text} intent="warning" layout="multiline">
          <MessageBarBody>{text}</MessageBarBody>
        </MessageBar>
      ))}
    </div>
  );
}
