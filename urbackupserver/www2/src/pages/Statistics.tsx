import { Spinner } from "@fluentui/react-components";
import { Suspense } from "react";
import { StorageUsage } from "../features/statistics/StorageUsage";

export const StatisticsPage = () => {
  return (
    <Suspense fallback={<Spinner />}>
      <div className="flow">
        <h3>Statistics</h3>
        <div className="flow">
          <h4>Storage Usage</h4>
          <StorageUsage />
        </div>
      </div>
    </Suspense>
  );
};
