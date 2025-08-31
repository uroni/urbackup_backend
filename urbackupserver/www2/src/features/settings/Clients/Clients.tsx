import {
  Badge,
  Body1,
  Caption2Strong,
  Field,
  SearchBox as FUISearchBox,
  Tab,
  TabList,
} from "@fluentui/react-components";
import { Link, Outlet } from "react-router-dom";

import { useSettings } from "../useSettings";
import styles from "./Clients.module.css";
import { SettingsClients } from "../../../api/urbackupserver";
import {
  filterBySearch,
  useFilteredBySearch,
} from "../../../components/SearchBox";

export function Clients() {
  const { navitems } = useSettings();

  const { setSearch, filteredItems } = useFilteredBySearch<SettingsClients>(
    navitems.clients,
    filterClientData,
  );

  const groups = groupClients(filteredItems);

  return (
    <div className={styles.wrapper}>
      {/* Sidebar */}
      <nav className={styles.sidebar}>
        <SearchClients onSearch={setSearch} />
        <ClientGroups groups={groups} />
      </nav>

      {/* Content */}
      <div className={styles.content}>
        <nav aria-labelledby="client-navigation">
          <div id="client-navigation" className="visually-hidden">
            Client menu
          </div>
          <TabList defaultSelectedValue="backups">
            <Tab value="backups">Backups</Tab>
            <Tab value="permissions">Permissions</Tab>
            <Tab value="client">Client</Tab>
            <Tab value="archive">Archive</Tab>
            <Tab value="alerts">Alerts</Tab>
            <Tab value="advanced">Advanced</Tab>
          </TabList>
        </nav>
        <section className={styles["client-content"]}>
          <Outlet />
        </section>
      </div>
    </div>
  );
}

function ClientGroups({ groups }: { groups: SettingsClients[][] }) {
  return (
    <ul>
      {groups.map((clients) => (
        <ClientList key={clients[0].groupname} clients={clients} />
      ))}
    </ul>
  );
}

function ClientList({ clients }: { clients: SettingsClients[] }) {
  if (clients.length === 0) {
    return null;
  }

  const groupName = clients[0].groupname || "Default";

  return (
    <>
      {groupName && (
        <div className={styles.group}>
          <Caption2Strong>{groupName}</Caption2Strong>
        </div>
      )}
      {clients.map((client) => (
        <li key={client.id} className={styles.client}>
          <Link to={`./${client.id}`}>
            <Body1>{client.name}</Body1>
            {client.override && (
              <div>
                <div className="visually-hidden">client has overrides</div>
                <Badge shape="circular" size="tiny" />
              </div>
            )}
          </Link>
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

function groupClients(clients: SettingsClients[]) {
  const grouped = Object.groupBy(clients, (d) => d.groupname);

  const result = Object.values(grouped) as SettingsClients[][];

  return result;
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
