import { Fragment } from "react";
import {
  Tab as FUITab,
  TabList,
  Body1,
  Link,
} from "@fluentui/react-components";
import { useNavigate } from "react-router-dom";

import { useSettings } from "./useSettings";
import { type TabItem, useSettingsTabs } from "./useSettingsTabs";
import styles from "./SettingsNavSidebar.module.css";

export const SettingsNavSidebar = () => {
  const { navitems } = useSettings();

  // Like the client tabs, the Internet settings only for users that may
  // change them
  const tabs = SETTINGS_TABS.map((t) => ({
    ...t,
    children: t.children?.filter(
      (c) => c.value !== "internet-active" || navitems.internet,
    ),
  }));

  const { selectedTab, onTabSelect } = useSettingsTabs(tabs);

  const navigate = useNavigate();

  return (
    <div className={styles.settingsTab}>
      <Link onClick={() => navigate("/")}>Back to app</Link>

      <div
        style={{
          marginBlockStart: "var(--spacingL)",
        }}
      >
        <TabList selectedValue={selectedTab} vertical onTabSelect={onTabSelect}>
          <SettingsTabs tabs={tabs} />
        </TabList>
      </div>
    </div>
  );
};

function SettingsTabs({
  tabs,
  baseValue = "",
}: {
  tabs: TabItem[];
  baseValue?: string;
}) {
  return (
    <>
      {tabs.map((st, i) => {
        if (st.children) {
          return (
            <Fragment key={i}>
              <h4
                style={{
                  color: "var(--colorNeutralForeground3)",
                  marginBlockStart: "var(--spacingXL)",
                  marginBlockEnd: "var(--spacingS)",
                }}
              >
                {st.title}
              </h4>
              <SettingsTabs tabs={st.children} baseValue={`${st.value}/`} />
            </Fragment>
          );
        }

        return (
          <FUITab key={st.value} value={`${baseValue}${st.value}`}>
            <Body1
              style={{
                color: "var(--colorNeutralForeground1)",
              }}
            >
              {st.title}
            </Body1>
          </FUITab>
        );
      })}
    </>
  );
}

const SETTINGS_TABS: TabItem[] = [
  {
    value: "server",
    title: "Server",
  },
  {
    value: "mail",
    title: "Mail",
  },
  {
    value: "ldap-ad",
    title: "LDAP/AD",
  },
  {
    value: "users",
    title: "Users",
  },
  {
    value: "clients",
    title: "Clients",
  },
  {
    value: "general",
    title: "General",
    children: [
      {
        value: "backups",
        title: "Backups",
      },
      {
        value: "permissions",
        title: "Permissions",
      },
      {
        value: "client",
        title: "Client",
      },
      {
        value: "archive",
        title: "Archive",
      },
      {
        value: "alerts",
        title: "Alerts",
      },
      {
        value: "local-passive",
        title: "Local/Passive",
      },
      {
        value: "internet-active",
        title: "Internet/Active",
      },
      {
        value: "advanced",
        title: "Advanced",
      },
    ],
  },
];
