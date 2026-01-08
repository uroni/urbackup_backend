import { useState } from "react";
import { SelectTabEvent, SelectTabData } from "@fluentui/react-components";

import { router } from "../../App";

export interface TabItem {
  value: string;
  title: React.ReactNode;
  children?: TabItem[];
}

const BASE_SETTINGS_URL = "/settings";

export function useSettingsTabs(tabs: TabItem[], baseURL = BASE_SETTINGS_URL) {
  const [selectedTab, setSelectedTab] = useState(() => getSelectedPage(tabs));

  const onTabSelect = async (_: SelectTabEvent, data: SelectTabData) => {
    setSelectedTab(data.value as string);

    const nt = `${baseURL}/${data.value}`;
    await router.navigate(nt);
  };

  return {
    selectedTab,
    onTabSelect,
  };
}

function getSelectedPage(settingsTabs: TabItem[]) {
  const page = window.location.hash;
  const settingsPage = page.replace(/#\/\w+\/?/, "");

  const pageInTabs = settingsTabs.find((st) => st.value === settingsPage);

  if (!pageInTabs || !settingsPage) {
    return settingsTabs[0].value;
  }

  return settingsPage;
}
