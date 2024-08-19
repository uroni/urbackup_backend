import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
} from "react";

import { StartBackupResultItem } from "../../api/urbackupserver";

type BackupResultContext = {
  updateBackupResults: (results: StartBackupResultItem[]) => void;
  getResultById: (id: number) => StartBackupResultItem | undefined;
};

const BackupResultContext = createContext<BackupResultContext | null>(null);

export const BackupResultProvider = ({
  children,
}: {
  children: React.ReactNode;
}) => {
  const [backupResults, setBackupResults] = useState<StartBackupResultItem[][]>(
    [],
  );

  // Clear oldest backupResults every 3s, if any
  useEffect(() => {
    let timeoutId: number;
    if (backupResults.length) {
      timeoutId = setTimeout(() => {
        setBackupResults((prev) => prev.slice(1));
      }, 3000);
    }

    return () => clearTimeout(timeoutId);
  }, [backupResults]);

  const getResultById = useCallback(
    (id: number) => backupResults.at(-1)?.find((b) => b.clientid === id),
    [backupResults],
  );

  const updateBackupResults = useCallback(
    (results: StartBackupResultItem[]) => {
      setBackupResults((prev) => [...prev, results]);
    },
    [],
  );

  const value = useMemo(
    () => ({
      updateBackupResults,
      getResultById,
    }),
    [backupResults, getResultById],
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
