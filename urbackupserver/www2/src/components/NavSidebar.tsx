import { useLocation, useNavigate } from "react-router-dom";
import {
  NavDrawer,
  NavDrawerBody,
  NavDrawerFooter,
  NavItem,
  OnNavItemSelectData,
} from "@fluentui/react-components";
import {
  ArchiveRegular,
  DataBarVerticalRegular,
  DocumentTextRegular,
  HeartPulseRegular,
  PulseRegular,
  SettingsRegular,
} from "@fluentui/react-icons";

import { Pages } from "../App";
import { SwitchToClassic } from "./SwitchToClassic";

export const NavSidebar = () => {
  const navigate = useNavigate();
  const { pathname } = useLocation();

  const onNavItemSelect = async (_: unknown, data: OnNavItemSelectData) => {
    await navigate(`/${data.value}`);
  };

  return (
    <NavDrawer
      open
      type="inline"
      size="small"
      selectedValue={getActivePage(pathname)}
      onNavItemSelect={onNavItemSelect}
    >
      <NavDrawerBody>
        <NavItem value={Pages.Status} icon={<HeartPulseRegular />}>
          Status
        </NavItem>
        <NavItem value={Pages.Activities} icon={<PulseRegular />}>
          Activities
        </NavItem>
        <NavItem value={Pages.Backups} icon={<ArchiveRegular />}>
          Backups
        </NavItem>
        <NavItem value={Pages.Statistics} icon={<DataBarVerticalRegular />}>
          Statistics
        </NavItem>
        <NavItem value={Pages.Logs} icon={<DocumentTextRegular />}>
          Logs
        </NavItem>
        <NavItem value={Pages.Settings} icon={<SettingsRegular />}>
          Settings
        </NavItem>
      </NavDrawerBody>
      <NavDrawerFooter>
        <SwitchToClassic />
      </NavDrawerFooter>
    </NavDrawer>
  );
};

export default NavSidebar;

function getActivePage(pathname: string) {
  return pathname.split("/").filter((p) => p.length)[0] ?? Pages.Status;
}
