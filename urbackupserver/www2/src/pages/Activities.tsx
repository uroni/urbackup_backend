import { makeStyles, Spinner, tokens } from "@fluentui/react-components";
import { Suspense } from "react";
import { useSuspenseQuery } from "@tanstack/react-query";

import { urbackupServer } from "../App";
import { LastActivitiesTable } from "../features/activities/LastActivitiesTable";

const useStyles = makeStyles({
  root: {
    display: "grid",
    gap: tokens.spacingHorizontalL,
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

  return (
    <Suspense fallback={<Spinner />}>
      <div className={classes.root}>
        <div>
          <h3 className={classes.heading}>Last Activities</h3>
        </div>
        <LastActivitiesTable data={lastacts} />
      </div>
    </Suspense>
  );
};
