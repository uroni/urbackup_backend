import { Suspense, useEffect, useRef, useState } from "react";
import { Button, Spinner, tokens } from "@fluentui/react-components";
import { useSuspenseQuery } from "@tanstack/react-query";

import { StorageUsage } from "../features/statistics/StorageUsage";
import { StorageUsageBreakdownTable } from "../features/statistics/StorageUsageBreakdownTable";
import { TotalStorageUsage } from "../features/statistics/TotalStorageUsage";
import { StorageAllocation } from "../features/statistics/StorageAllocation";
import { urbackupServer } from "../App";

const STORAGE_USAGE_CHART_HEIGHT = 200;
const STORAGE_USAGE_CHART_PADDING = 50;

const styles: Record<string, React.CSSProperties> = {
  root: {
    display: "grid",
    gap: tokens.spacingVerticalXXL,
    gridTemplateColumns: "1fr 320px",
  },
  storageUsage: {
    overflow: "hidden",
    width: "clamp(300px, 60vw, 800px)",
  },
  totalStorageUsage: {
    display: "flex",
    flexDirection: "column",
    background: tokens.colorNeutralCardBackground,
    padding: tokens.spacingHorizontalL,
    borderRadius: tokens.borderRadiusLarge,
  },
  recalculateButton: {
    marginBlockStart: "auto",
  },
};

export const StatisticsPage = () => {
  const ref = useRef<HTMLDivElement | null>(null);

  const [width, setWidth] = useState(0);

  const [recalculateStatistics, setRecalculateStatistics] = useState(false);

  const storageUsageStatsResult = useSuspenseQuery({
    queryKey: ["storage-usage", recalculateStatistics],
    queryFn: () =>
      recalculateStatistics
        ? urbackupServer.recalculateStats()
        : urbackupServer.getUsageStats(),
  });

  const { usage, reset_statistics } = storageUsageStatsResult.data!;

  useEffect(() => {
    function handleResize() {
      if (ref.current) {
        const rect = ref.current.getBoundingClientRect();
        setWidth(rect.width);
      }
    }

    window.addEventListener("resize", handleResize);

    return () => {
      window.removeEventListener("resize", handleResize);
    };
  }, []);

  return (
    <Suspense fallback={<Spinner />}>
      <div className="flow">
        <h3>Statistics</h3>
        <div style={styles.root}>
          <div ref={ref} className="flow" style={styles.storageUsage}>
            <h4>Storage Usage</h4>
            <div
              style={{
                height: `${STORAGE_USAGE_CHART_HEIGHT + STORAGE_USAGE_CHART_PADDING}px`,
              }}
            >
              <Suspense fallback={<Spinner />}>
                <StorageUsage
                  width={width}
                  height={STORAGE_USAGE_CHART_HEIGHT}
                />
              </Suspense>
            </div>
          </div>
          <div className="flow" style={styles.totalStorageUsage}>
            <h4>Total Storage Usage</h4>
            <TotalStorageUsage usage={usage} />
            <Button
              onClick={() => {
                if (reset_statistics) {
                  setRecalculateStatistics(true);
                }
              }}
              style={styles.recalculateButton}
            >
              Recalculate Statistics
            </Button>
          </div>

          <StorageUsageBreakdownTable data={usage} />

          <div ref={ref} className="flow">
            <h4>Storage Allocation</h4>
            <div>
              <StorageAllocation />
            </div>
          </div>
        </div>
      </div>
    </Suspense>
  );
};
