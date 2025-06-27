import { useState } from "react";
import {
  SelectTabEvent,
  SelectTabData,
  Caption1,
  Button,
} from "@fluentui/react-components";
import { Add16Regular } from "@fluentui/react-icons";

import { router } from "../../App";
import { useSettings } from "./useSettings";
import { SettingsNavitems } from "../../api/urbackupserver";
import { SelectClientCombobox } from "../../components/SelectClientCombobox";

export interface Tab {
  value: string;
  title: React.ReactNode;
  children?: Tab[];
  afterChildren?: React.ReactNode;
}

const BASE_SETTINGS_URL = "/settings";

export function useSettingsTabs() {
  const { navitems } = useSettings();

  const [selectedTab, setSelectedTab] = useState(() =>
    getSelectedPage(BASE_SETTINGS_TABS),
  );

  const onTabSelect = async (_: SelectTabEvent, data: SelectTabData) => {
    setSelectedTab(data.value as string);

    const nt = `${BASE_SETTINGS_URL}/${data.value}`;
    await router.navigate(nt);
  };

  const settingsTabs = updateSettingsTabs(BASE_SETTINGS_TABS, navitems);

  return {
    settingsTabs,
    selectedTab,
    onTabSelect,
  };
}

function addGroupsToSettingsNav(
  settingsTabs: Tab[],
  navItems: SettingsNavitems,
) {
  const newNavItems = navItems.groups
    .filter((g) => g.name.length)
    .reduce((all, group) => {
      return [...all, { value: group.name, title: group.name }];
    }, [] as Tab[]);

  const routes = {
    value: "groups",
    title: "Groups",
    children: newNavItems,
    afterChildren: (
      <Button
        appearance="subtle"
        icon={<Add16Regular />}
        style={{
          justifyContent: "start",
          paddingInlineStart: "var(--spacingXS)",
          color: "var(--colorNeutralForeground3)",
        }}
      >
        Add new group
      </Button>
    ),
  };

  return [...settingsTabs, routes];
}

function addClientsToSettingsNav(
  settingsTabs: Tab[],
  navItems: SettingsNavitems,
) {
  const newNavItems = navItems.clients
    .filter((c) => c.override)
    .reduce((all, client) => {
      return [
        ...all,
        {
          value: client.name,
          title: client.group ? (
            <span className="cluster">
              {client.name} <Caption1>({client.groupname})</Caption1>
            </span>
          ) : (
            client.name
          ),
        },
      ];
    }, [] as Tab[]);

  const routes = {
    value: "clients",
    title: "Clients",
    children: newNavItems,
    afterChildren: (
      <SelectClientCombobox
        clients={navItems.clients.filter((c) => !c.override)}
        onSelect={(id) => console.log(id)}
        defaultValue=""
        showLabel={false}
        placeholder="Add client settings"
        hideAllClients
      />
    ),
  };

  return [...settingsTabs, routes];
}

function updateSettingsTabs(settingsTabs: Tab[], navItems: SettingsNavitems) {
  const groupSettings = addGroupsToSettingsNav(settingsTabs, navItems);
  const clientSettings = addClientsToSettingsNav(groupSettings, navItems);

  return clientSettings;
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
