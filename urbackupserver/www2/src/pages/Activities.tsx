import { Spinner } from "@fluentui/react-components";
import { Suspense } from "react";
import { useSuspenseQuery } from "@tanstack/react-query";

import { urbackupServer } from "../App";
import { LastActivitiesTable } from "../features/activities/LastActivitiesTable";
import { OngoingActivitiesTable } from "../features/activities/OngoingActivitiesTable";
import { TableWrapper } from "../components/TableWrapper";

const REFETCH_INTERVAL = 5000;

export const ActivitiesPage = () => {
  const progressResult = useSuspenseQuery({
    queryKey: ["progress"],
    queryFn: () => urbackupServer.progress(true),
    refetchInterval: REFETCH_INTERVAL,
  });

  const lastacts = progressResult.data!.lastacts;
  const progress = progressResult.data!.progress;

  return (
    <Suspense fallback={<Spinner />}>
      <div
        className="flow"
        style={{ "--flow-space": "4em" } as React.CSSProperties}
      >
        <div>
          <TableWrapper>
            <h3>Activities</h3>
            <OngoingActivitiesTable data={progress} />
          </TableWrapper>
        </div>
        <div>
          <TableWrapper>
            <h3>Last Activities</h3>
            <LastActivitiesTable data={lastacts} />
          </TableWrapper>
        </div>
      </div>
    </Suspense>
  );
};
