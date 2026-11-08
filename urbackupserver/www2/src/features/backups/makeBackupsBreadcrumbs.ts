import { BASE_HREF, BreadcrumbItem } from "../../components/Breadcrumbs";
import { formatDatetime } from "../../utils/format";

export function makeBackupsBreadcrumbs({
  clientId,
  clientName,
  backupId,
  backuptime,
  path,
}: {
  clientId: number;
  clientName: string;
  backupId?: number;
  backuptime?: number;
  path?: string | null;
}) {
  const backupsUrl = `${BASE_HREF}/backups`;
  const clientUrl = `${backupsUrl}/${clientId}`;

  const baseBreadcrumbItems: BreadcrumbItem[] = [
    {
      key: "backups",
      text: "Backups",
      itemProps: {
        href: backupsUrl,
      },
    },
    {
      key: `client-${clientId}`,
      text: clientName,
      itemProps: {
        href: clientUrl,
      },
    },
  ];

  if (!backuptime) {
    return baseBreadcrumbItems;
  }

  const backupUrl = `${clientUrl}/${backupId}`;

  const backupTimeBreadCrumb = {
    key: `backup-${backupId}`,
    text: formatDatetime(backuptime),
    itemProps: {
      href: backupUrl,
    },
  };

  baseBreadcrumbItems.push(backupTimeBreadCrumb);

  if (!path) {
    return baseBreadcrumbItems;
  }

  const breadcrumbPathItems = makePathBreadcrumbItems(backupUrl, path);

  return [...baseBreadcrumbItems, ...breadcrumbPathItems];
}

function makePathBreadcrumbItems(backupUrl: string, path: string) {
  return path
    ?.split("/")
    .filter(Boolean)
    .map<BreadcrumbItem>((current, index, array) => {
      const currentPath = array.slice(0, index + 1).join("/");

      return {
        key: crypto.randomUUID(),
        text: current,
        itemProps: {
          href: `${backupUrl}?path=${currentPath}`,
        },
      };
    });
}
