import { Suspense, useEffect, useRef, useState } from "react";
import { Spinner } from "@fluentui/react-components";

import { StorageUsage } from "../features/statistics/StorageUsage";
import { StorageUsageBreakdownTable } from "../features/statistics/StorageUsageBreakdownTable";

const STORAGE_USAGE_CHART_HEIGHT = 300;

export const StatisticsPage = () => {
  const ref = useRef<HTMLDivElement | null>(null);

  const [width, setWidth] = useState(0);

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
              height: `${STORAGE_USAGE_CHART_HEIGHT + 50}px`,
            }}
          >
            <Suspense fallback={<Spinner />}>
              <StorageUsage width={width} height={STORAGE_USAGE_CHART_HEIGHT} />
            </Suspense>
          </div>
        </div>
      </div>
      <div>
        <StorageUsageBreakdownTable />
      </div>
    </Suspense>
  );
};
