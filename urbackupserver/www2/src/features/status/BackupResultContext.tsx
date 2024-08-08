import React, {
  createContext,
  useCallback,
  useContext,
  useMemo,
  useState,
} from "react";

import { StartBackupResultItem } from "../../api/urbackupserver";

type BackupResultContext = {
  updateBackupResults: React.Dispatch<
    React.SetStateAction<StartBackupResultItem[]>
  >;
  getResultById: (id: number) => StartBackupResultItem | undefined;
  clearResultById: (
    id: number,
    startType: StartBackupResultItem["start_type"],
  ) => void;
};

const BackupResultContext = createContext<BackupResultContext | null>(null);

export const BackupResultProvider = ({
  children,
}: {
  children: React.ReactNode;
}) => {
  const [backupResults, setBackupResults] = useState<StartBackupResultItem[]>(
    [],
  );

  const getResultById = useCallback(
    (id: number) => backupResults.find((b) => b.clientid === id),
    [backupResults],
  );

  const clearResultById = useCallback(
    (id: number, startType: StartBackupResultItem["start_type"]) =>
      setBackupResults((prev) =>
        prev.filter((b) => b.clientid !== id && b.start_type !== startType),
      ),
    [],
  );

  const value = useMemo(
    () => ({
      updateBackupResults: setBackupResults,
      getResultById,
      clearResultById,
    }),
    [backupResults, getResultById, clearResultById],
  );

  return (
    <BackupResultContext.Provider value={value}>
      {children}
    </BackupResultContext.Provider>
  );
};

export const useBackupResult = () => {
  const context = useContext(BackupResultContext);
  if (!context) {
    throw new Error(
      "useBackupResult must be used within a BackupResultProvider",
    );
  }
  return context;
};
