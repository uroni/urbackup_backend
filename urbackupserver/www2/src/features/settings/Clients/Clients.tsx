import { useState } from "react";
import {
  Badge,
  Body1,
  Button,
  Caption2Strong,
  Field,
  SearchBox as FUISearchBox,
  Input,
  Tab,
  TabList,
} from "@fluentui/react-components";
import { useQueryClient } from "@tanstack/react-query";
import { NavLink, Outlet, useNavigate, useParams } from "react-router-dom";

import { useSettings } from "../useSettings";
import styles from "./Clients.module.css";
import type {
  SettingsClients,
  SettingsGroups,
} from "../../../api/urbackupserver";
import { urbackupServer } from "../../../App";
import { addMessage } from "../../../components/Banner/messageStore";
import {
  filterBySearch,
  useFilteredBySearch,
} from "../../../components/SearchBox";
import { useSettingsTabs } from "../useSettingsTabs";

export function Clients() {
  const { navitems } = useSettings();

  const { clientId = "" } = useParams();

  // Like the old web interface, only users that may change the Internet
  // settings see them
  const tabs = CLIENT_TABS.filter(
    (t) => t.value !== "internet-active" || navitems.internet,
  );

  const { selectedTab, onTabSelect } = useSettingsTabs(
    tabs,
    `/settings/clients/${clientId}`,
  );

  const { search, setSearch, filteredItems } =
    useFilteredBySearch<SettingsClients>(navitems.clients, filterClientData);

  // Groups without clients only without a search, to open their settings
  const groups = navitems.groups
    .map((group) => ({
      ...group,
      clients: filteredItems.filter((c) => c.group === group.id),
    }))
    .filter((g) => g.clients.length > 0 || search === "");

  return (
    <div className={styles.wrapper}>
      {/* Sidebar */}
      <nav className={styles.sidebar}>
        <SearchClients onSearch={setSearch} />
        {navitems.groupmod && <AddGroup />}
        <ClientGroups groups={groups} />
      </nav>

      {/* Content */}
      <div className={styles.content}>
        <nav aria-labelledby="client-navigation">
          <div id="client-navigation" className="visually-hidden">
            Client menu
          </div>
          <div className={styles["tab-bar"]}>
            <TabList
              selectedValue={selectedTab}
              onTabSelect={onTabSelect}
              className={styles.tablist}
            >
              {tabs.map(({ value, title }) => (
                <Tab key={value} value={value}>
                  {title}
                </Tab>
              ))}
            </TabList>
            {+clientId < 0 && navitems.groupmod && (
              <DeleteGroup groupId={-clientId} />
            )}
          </div>
        </nav>
        <section className={styles["client-content"]}>
          <Outlet />
        </section>
      </div>
    </div>
  );
}

function ClientGroups({
  groups,
}: {
  groups: (SettingsGroups & { clients: SettingsClients[] })[];
}) {
  return (
    <ul>
      {groups.map((group) => (
        <ClientList key={group.id} group={group} clients={group.clients} />
      ))}
    </ul>
  );
}

function ClientList({
  group,
  clients,
}: {
  group: SettingsGroups;
  clients: SettingsClients[];
}) {
  return (
    <>
      <div className={styles.group}>
        <NavLink
          // The settings of the default group are the general settings
          to={
            group.id === 0
              ? "/settings/general/backups"
              : `./-${group.id}/backups`
          }
          title={`Settings of the group ${group.name || "Default"}`}
        >
          <Caption2Strong>{group.name || "Default"}</Caption2Strong>
        </NavLink>
      </div>
      {clients.map((client) => (
        <li key={client.id} className={styles.client}>
          <NavLink to={`./${client.id}/backups`}>
            <Body1>{client.name}</Body1>
            {client.override && (
              <div>
                <div className="visually-hidden">client has overrides</div>
                <Badge shape="circular" size="tiny" />
              </div>
            )}
          </NavLink>
        </li>
      ))}
    </>
  );
}

function SearchClients({ onSearch }: { onSearch: (search: string) => void }) {
  return (
    <Field label="Search clients" className={styles.search}>
      <FUISearchBox
        autoComplete="off"
        onChange={(_, data) => {
          onSearch(data.value);
        }}
      />
    </Field>
  );
}

function AddGroup() {
  const [name, setName] = useState("");

  const queryClient = useQueryClient();
  const navigate = useNavigate();

  const add = async (e: React.FormEvent) => {
    e.preventDefault();

    try {
      const group = await urbackupServer.addGroup(name);
      setName("");
      await queryClient.invalidateQueries({ queryKey: ["settings"] });
      navigate(`/settings/clients/-${group.id}/backups`);
    } catch {
      addMessage({
        intent: "error",
        text: `Adding the group failed. Is there a group named "${name}" already?`,
      });
    }
  };

  return (
    <form className={styles["add-group"]} onSubmit={add}>
      <Input
        aria-label="New group name"
        placeholder="New group name"
        value={name}
        onChange={(_, d) => setName(d.value)}
      />
      <Button type="submit" disabled={name.trim() === ""}>
        Add group
      </Button>
    </form>
  );
}

function DeleteGroup({ groupId }: { groupId: number }) {
  const queryClient = useQueryClient();
  const navigate = useNavigate();

  const remove = async () => {
    if (
      !window.confirm(
        "Delete this group? Its clients move to the default group.",
      )
    ) {
      return;
    }

    try {
      await urbackupServer.removeGroup(groupId);
      await queryClient.invalidateQueries({ queryKey: ["settings"] });
      navigate("/settings/clients");
    } catch {
      addMessage({ intent: "error", text: "Deleting the group failed." });
    }
  };

  return (
    <Button appearance="subtle" onClick={remove}>
      Delete group
    </Button>
  );
}

function filterClientData(item: SettingsClients, search: string) {
  const { name, groupname } = item;

  // Search in fields as displayed in the table
  const searchableFields = {
    name,
    groupname,
  };

  return filterBySearch(search.toLowerCase(), searchableFields);
}

const CLIENT_TABS = [
  {
    value: "backups",
    title: "Backups",
  },
  {
    value: "permissions",
    title: "Permissions",
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
    value: "local-passive",
    title: "Local/Passive Client",
  },
  {
    value: "internet-active",
    title: "Internet/Active Client",
  },
  {
    value: "advanced",
    title: "Advanced",
  },
];
