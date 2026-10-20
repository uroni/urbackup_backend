import { SelectTabEvent, SelectTabData } from "@fluentui/react-components";
import { useLocation } from "react-router-dom";

import { router } from "../../App";

export interface TabItem {
  value: string;
  title: React.ReactNode;
  children?: TabItem[];
}

const BASE_SETTINGS_URL = "/settings";

export function useSettingsTabs(tabs: TabItem[], baseURL = BASE_SETTINGS_URL) {
  const { pathname } = useLocation();

  const onTabSelect = async (_: SelectTabEvent, data: SelectTabData) => {
    const nt = `${baseURL}/${data.value}`;
    await router.navigate(nt);
  };

  return {
    // Follow the route, so links and back/forward update the selection too
    selectedTab: getSelectedPage(tabs, pathname),
    onTabSelect,
  };
}

function getSelectedPage(settingsTabs: TabItem[], pathname: string) {
  const settingsPage = pathname.replace(/^\/\w+\/?/, "");
  const segments = settingsPage.split("/");

  // Tabs in a group, e.g. "general/backups", are selected by their full page
  const allTabs = settingsTabs.flatMap((st) =>
    st.children
      ? st.children.map((c) => ({ ...c, value: `${st.value}/${c.value}` }))
      : [st],
  );

  // "clients/7/permissions" selects "clients" in the sidebar and
  // "permissions" in the client tabs
  const pageInTabs = allTabs.find(
    (st) =>
      st.value === settingsPage ||
      st.value === segments[0] ||
      st.value === segments[segments.length - 1],
  );

  return pageInTabs?.value ?? allTabs[0].value;
}
