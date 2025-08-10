import { Fragment } from "react";
import {
  Tab as FUITab,
  TabList,
  Body1,
  Link,
} from "@fluentui/react-components";
import { useNavigate } from "react-router-dom";

import { type Tab, useSettingsTabs } from "./useSettingsTabs";
import styles from "./SettingsNavSidebar.module.css";

export const SettingsNavSidebar = () => {
  const { settingsTabs, selectedTab, onTabSelect } = useSettingsTabs();

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
          <SettingsTabs tabs={settingsTabs} />
        </TabList>
      </div>
    </div>
  );
};

function SettingsTabs({
  tabs,
  baseValue = "",
}: {
  tabs: Tab[];
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
              {st?.afterChildren && st.afterChildren}
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
