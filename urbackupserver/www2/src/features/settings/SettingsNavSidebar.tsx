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
  LaptopRegular,
  MailRegular,
  PeopleTeamRegular,
  PersonRegular,
  ServerRegular,
  WrenchRegular,
} from "@fluentui/react-icons";
import { useLocation, useNavigate } from "react-router-dom";

import { type TabItem } from "./useSettingsTabs";
import { SwitchToClassic } from "../../components/SwitchToClassic";

const BACK_TO_APP = "__back";

export const SettingsNavSidebar = () => {
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
        <SettingsItems tabs={SETTINGS_TABS} />
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

  // /settings shows the first page; pages without an entry select none ("",
  // not undefined, which would make the selection uncontrolled)
  return settingsPage === "" ? values[0] : selected ?? "";
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
        value: "advanced",
        title: "Advanced",
        icon: <WrenchRegular />,
      },
    ],
  },
];
