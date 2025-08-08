import { useState } from "react";
import { useLocation, useNavigate } from "react-router-dom";
import {
  SelectTabData,
  SelectTabEvent,
  Tab,
  TabList,
} from "@fluentui/react-components";

import { Pages } from "../App";

export const NavSidebar = () => {
  const navigate = useNavigate();
  const { pathname } = useLocation();

  const [selectedValue, setSelectedValue] = useState(() =>
    getInitialTab(pathname),
  );

  const onTabSelect = async (event: SelectTabEvent, data: SelectTabData) => {
    setSelectedValue(data.value as Pages);

    const nt = `/${data.value}`;
    await navigate(nt);
  };

  return (
    <TabList selectedValue={selectedValue} vertical onTabSelect={onTabSelect}>
      <Tab value={Pages.Status}>Status</Tab>
      <Tab value={Pages.Activities}>Activities</Tab>
      <Tab value={Pages.Backups}>Backups</Tab>
      <Tab value={Pages.Statistics}>Statistics</Tab>
      <Tab value={Pages.Logs}>Logs</Tab>
      <Tab value={Pages.Settings}>Settings</Tab>
    </TabList>
  );
};

export default NavSidebar;

function getInitialTab(pathname: string) {
  const page = pathname.split("/").filter((p) => p.length)[0] ?? Pages.Status;
  return page;
}
