import { Suspense, useEffect, useRef, useState } from "react";
import { Button, Spinner } from "@fluentui/react-components";
import { useSuspenseQuery } from "@tanstack/react-query";

import { StorageUsage } from "../features/statistics/StorageUsage";
import { StorageUsageBreakdownTable } from "../features/statistics/StorageUsageBreakdownTable";
import { TotalStorageUsage } from "../features/statistics/TotalStorageUsage";
import { urbackupServer } from "../App";

const STORAGE_USAGE_CHART_HEIGHT = 300;
const STORAGE_USAGE_CHART_PADDING = 50;

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
        <div
          className="flow"
          ref={ref}
          style={{
            overflow: "hidden",
            width: "clamp(500px, 84vw, 1200px)",
          }}
        >
          <h4>Storage Usage</h4>
          <div
            style={{
              height: `${STORAGE_USAGE_CHART_HEIGHT + STORAGE_USAGE_CHART_PADDING}px`,
            }}
          >
            <Suspense fallback={<Spinner />}>
              <StorageUsage width={width} height={STORAGE_USAGE_CHART_HEIGHT} />
            </Suspense>
          </div>
        </div>
      </div>
      <div className="flow">
        <div className="flow">
          <div className="cluster" data-justify-content="space-between">
            <h4>Total Storage Usage</h4>
            <Button
              size="small"
              onClick={() => {
                if (reset_statistics) {
                  setRecalculateStatistics(true);
                }
              }}
            >
              Recalculate Statistics
            </Button>
          </div>
          <TotalStorageUsage usage={usage} />
        </div>

        <StorageUsageBreakdownTable data={usage} />
      </div>
    </Suspense>
  );
};
