import { Pages, router, state } from "../App";
import { useSnapshot } from "valtio";
import {
  SelectTabData,
  SelectTabEvent,
  Tab,
  TabList,
} from "@fluentui/react-components";

export const NavSidebar = () => {
  const snap = useSnapshot(state);

  const onTabSelect = async (event: SelectTabEvent, data: SelectTabData) => {
    const nt = `/${data.value}`;
    await router.navigate(nt);
  };

  return (
    <TabList selectedValue={snap.activePage} vertical onTabSelect={onTabSelect}>
      <Tab value={Pages.Status}>Status</Tab>
      <Tab value={Pages.Activities}>Activities</Tab>
      <Tab value={Pages.Backups}>Backups</Tab>
      <Tab value={Pages.Statistics}>Statistics</Tab>
    </TabList>
  );
};

export default NavSidebar;
