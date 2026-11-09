import { Fragment } from "react";
import {
  NavDivider,
  NavDrawer,
  NavDrawerBody,
  NavDrawerFooter,
  NavItem,
  NavSectionHeader,
  OnNavItemSelectData,
} from "@fluentui/react-components";
import {
  AlertRegular,
  ArchiveRegular,
  ArrowLeftRegular,
  BoxRegular,
  DesktopRegular,
  GlobeRegular,
  HomeRegular,
  LaptopRegular,
  MailRegular,
  PeopleTeamRegular,
  PersonRegular,
  ServerRegular,
  ShieldRegular,
  WrenchRegular,
} from "@fluentui/react-icons";
import { useLocation, useNavigate } from "react-router-dom";

import { useSettings } from "./useSettings";
import { type TabItem } from "./useSettingsTabs";
import { SwitchToClassic } from "../../components/SwitchToClassic";

const BACK_TO_APP = "__back";

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

  const navigate = useNavigate();
  const { pathname } = useLocation();

  const onNavItemSelect = async (_: unknown, data: OnNavItemSelectData) => {
    if (data.value === BACK_TO_APP) {
      await navigate("/");
      return;
    }

    await navigate(`/settings/${data.value}`);
  };

  return (
    <NavDrawer
      open
      type="inline"
      size="small"
      selectedValue={getSelectedPage(pathname)}
      onNavItemSelect={onNavItemSelect}
    >
      <NavDrawerBody>
        <NavItem value={BACK_TO_APP} icon={<ArrowLeftRegular />}>
          Back to app
        </NavItem>
        <NavDivider />
        <SettingsItems tabs={tabs} />
      </NavDrawerBody>
      <NavDrawerFooter>
        <SwitchToClassic />
      </NavDrawerFooter>
    </NavDrawer>
  );
};

function SettingsItems({
  tabs,
  baseValue = "",
}: {
  tabs: TabItem[];
  baseValue?: string;
}) {
  return (
    <>
      {tabs.map((st) => {
        if (st.children) {
          return (
            <Fragment key={st.value}>
              <NavSectionHeader>{st.title}</NavSectionHeader>
              <SettingsItems
                tabs={st.children}
                baseValue={`${baseValue}${st.value}/`}
              />
            </Fragment>
          );
        }

        return (
          <NavItem
            key={st.value}
            value={`${baseValue}${st.value}`}
            icon={st.icon}
          >
            {st.title}
          </NavItem>
        );
      })}
    </>
  );
}

// "clients/7/permissions" selects "clients", "general/backups" selects
// "general/backups"
function getSelectedPage(pathname: string) {
  const settingsPage = pathname.replace(/^\/settings\/?/, "");

  const values = flattenValues(SETTINGS_TABS);
  const selected = values.find(
    (v) => settingsPage === v || settingsPage.startsWith(`${v}/`),
  );

  return selected ?? values[0];
}

function flattenValues(tabs: TabItem[], baseValue = ""): string[] {
  return tabs.flatMap((st) =>
    st.children
      ? flattenValues(st.children, `${baseValue}${st.value}/`)
      : [`${baseValue}${st.value}`],
  );
}

const SETTINGS_TABS: TabItem[] = [
  {
    value: "server",
    title: "Server",
    icon: <ServerRegular />,
  },
  {
    value: "mail",
    title: "Mail",
    icon: <MailRegular />,
  },
  {
    value: "ldap-ad",
    title: "LDAP/AD",
    icon: <PeopleTeamRegular />,
  },
  {
    value: "users",
    title: "Users",
    icon: <PersonRegular />,
  },
  {
    value: "clients",
    title: "Clients",
    icon: <DesktopRegular />,
  },
  {
    value: "general",
    title: "General",
    children: [
      {
        value: "backups",
        title: "Backups",
        icon: <ArchiveRegular />,
      },
      {
        value: "permissions",
        title: "Permissions",
        icon: <ShieldRegular />,
      },
      {
        value: "client",
        title: "Client",
        icon: <LaptopRegular />,
      },
      {
        value: "archive",
        title: "Archive",
        icon: <BoxRegular />,
      },
      {
        value: "alerts",
        title: "Alerts",
        icon: <AlertRegular />,
      },
      {
        value: "local-passive",
        title: "Local/Passive",
        icon: <HomeRegular />,
      },
      {
        value: "internet-active",
        title: "Internet/Active",
        icon: <GlobeRegular />,
      },
      {
        value: "advanced",
        title: "Advanced",
        icon: <WrenchRegular />,
      },
    ],
  },
];
