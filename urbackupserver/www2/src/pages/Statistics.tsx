import { Spinner } from "@fluentui/react-components";
import { Suspense } from "react";

export const StatisticsPage = () => {
  return (
    <Suspense fallback={<Spinner />}>
      <div className="flow">
        <h3>Statistics</h3>
      </div>
    </Suspense>
  );
};
