import { useState } from "react";
import { useNavigate } from "react-router-dom";
import {
  SelectTabData,
  SelectTabEvent,
  Tab,
  TabList,
} from "@fluentui/react-components";

import { Pages } from "../App";

export const NavSidebar = () => {
  const [selectedValue, setSelectedValue] = useState(Pages.Status);

  const navigate = useNavigate();

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
