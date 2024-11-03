import { makeStyles, Spinner, tokens } from "@fluentui/react-components";
import { Suspense } from "react";
import { useSuspenseQuery } from "@tanstack/react-query";

import { urbackupServer } from "../App";
import { LastActivitiesTable } from "../features/activities/LastActivitiesTable";
import { OngoingActivitiesTable } from "../features/activities/OngoingActivitiesTable";

const useStyles = makeStyles({
  root: {
    display: "grid",
    gap: tokens.spacingVerticalXXXL,
  },
  heading: {
    marginBlockStart: 0,
  },
});

const REFETCH_INTERVAL = 5000;

export const ActivitiesPage = () => {
  const progressResult = useSuspenseQuery({
    queryKey: ["progress"],
    queryFn: () => urbackupServer.progress(true),
    refetchInterval: REFETCH_INTERVAL,
  });

  const classes = useStyles();

  const lastacts = progressResult.data!.lastacts;
  const progress = progressResult.data!.progress;

  return (
    <Suspense fallback={<Spinner />}>
      <div className={classes.root}>
        <section>
          <h3 className={classes.heading}>Activities</h3>
          <OngoingActivitiesTable data={progress} />
        </section>
        <section>
          <h3 className={classes.heading}>Last Activities</h3>
          <LastActivitiesTable data={lastacts} />
        </section>
      </div>
    </Suspense>
  );
};
