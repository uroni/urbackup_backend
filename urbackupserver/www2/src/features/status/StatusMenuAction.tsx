import { StatusClientItem } from "../../api/urbackupserver";
import { StatusMenu } from "./StatusMenu";
import { useStatusClientActions } from "./useStatusClientActions";

export function StatusMenuAction({
  idList,
  children,
  trigger,
}: {
  idList: StatusClientItem["id"][];
  children?: (props: {
    removeClients: (idList: StatusClientItem["id"][]) => void;
  }) => React.ReactNode;
  trigger?: React.ReactElement;
}) {
  const { removeClients, startBackup } = useStatusClientActions();

  return (
    <StatusMenu
      onBackup={(type) => startBackup(type, idList)}
      trigger={trigger}
    >
      {children && children({ removeClients })}
    </StatusMenu>
  );
}
