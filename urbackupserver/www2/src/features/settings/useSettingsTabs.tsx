import { useState } from "react";
import { SelectTabEvent, SelectTabData } from "@fluentui/react-components";

import { router } from "../../App";

export interface Tab {
  value: string;
  title: React.ReactNode;
  children?: Tab[];
  afterChildren?: React.ReactNode;
}

const BASE_SETTINGS_URL = "/settings";

export function useSettingsTabs() {
  const [selectedTab, setSelectedTab] = useState(() =>
    getSelectedPage(BASE_SETTINGS_TABS),
  );

  const onTabSelect = async (_: SelectTabEvent, data: SelectTabData) => {
    setSelectedTab(data.value as string);

    const nt = `${BASE_SETTINGS_URL}/${data.value}`;
    await router.navigate(nt);
  };

  const settingsTabs = BASE_SETTINGS_TABS;

  return {
    settingsTabs,
    selectedTab,
    onTabSelect,
  };
}

function getSelectedPage(settingsTabs: Tab[]) {
  const page = window.location.hash;
  const settingsPage = page.replace(/#\/\w+\/?/, "");

  if (!settingsPage) {
    return settingsTabs[0].value;
  }

  return settingsPage;
}

export const BASE_SETTINGS_TABS: Tab[] = [
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
        value: "advanced",
        title: "Advanced",
      },
    ],
  },
];
