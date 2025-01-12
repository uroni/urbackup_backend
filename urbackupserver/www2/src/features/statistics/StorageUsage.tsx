import { useState } from "react";
import { useSuspenseQuery } from "@tanstack/react-query";
import {
  IChartProps,
  ILineChartStyles,
  LineChart,
} from "@fluentui/react-charting";
import { Tab, TabList, tokens } from "@fluentui/react-components";

import { urbackupServer } from "../../App";
import { SelectStorageUsageClient } from "./SelectStorageUsageClient";

const DURATIONS = {
  d: "Day",
  m: "Month",
  y: "Year",
} as const;

const lineChartStyles: ILineChartStyles = {
  root: {
    overflow: "hidden",
    line: {
      strokeWidth: "1px",
    },
  },
  chartWrapper: {
    overflow: "hidden",
  },
  axisTitle: {
    fill: "currentColor",
    color: "currentColor",
  },
  yAxis: {
    text: {
      fill: "currentColor",
    },
    line: {
      stroke: "currentColor",
    },
  },
  xAxis: {
    text: {
      fill: "currentColor",
    },
  },
  calloutContentRoot: {
    background: "inherit",
  },
  calloutBlockContainer: {
    borderLeft: "0",
    margin: 0,
    padding: 0,
    color: "currentColor",
  },
  calloutContentX: {
    color: "currentColor",
  },
  calloutlegendText: {
    display: "none",
  },
};

export function StorageUsage({
  width,
  height,
}: {
  width: number;
  height: number;
}) {
  const [duration, setDuration] =
    useState<Parameters<typeof urbackupServer.getUsageGraphData>[0]>("d");

  const [selectedClientId, setSelectedClientId] = useState<
    string | undefined
  >();

  const storageUsageResult = useSuspenseQuery({
    queryKey: ["usage-graph", duration, selectedClientId],
    queryFn: () => urbackupServer.getUsageGraphData(duration, selectedClientId),
  });

  const clientsResult = useSuspenseQuery({
    queryKey: ["clients"],
    queryFn: () => urbackupServer.getClients(),
  });

  const clients = clientsResult.data;

  const storageUsage = storageUsageResult.data!;

  if (!storageUsage.length) {
    return <span>No storage in use</span>;
  }

  const limits = {
    min: Math.min(...storageUsage.map((d) => d.data)),
    max: Math.max(...storageUsage.map((d) => d.data)),
  };

  const data: IChartProps = {
    chartTitle: "Storage Usage",
    lineChartData: [
      {
        legend: "Storage Usage",
        data: storageUsage.map((d) => ({
          x: new Date(d.xlabel),
          y: d.data,
          xAxisCalloutData: new Intl.DateTimeFormat("en-US", {
            dateStyle: "long",
          }).format(Date.parse(d.xlabel)),
          yAxisCalloutData: `${d.data.toFixed(0)} MB`,
        })),
        color: tokens.colorBrandBackground,
      },
    ],
  };

  return (
    <div className="flow">
      <div
        className="cluster"
        style={
          {
            "--cluster-horizontal-alignment": "space-between",
          } as React.CSSProperties
        }
      >
        <div>
          <SelectStorageUsageClient
            clients={clients}
            onSelect={(id) => setSelectedClientId(id)}
          />
        </div>
        <TabList
          size="small"
          defaultSelectedValue={duration}
          selectTabOnFocus={true}
          onTabSelect={(_, data) => {
            setDuration(data.value as typeof duration);
          }}
        >
          <Tab value="d">{DURATIONS.d}</Tab>
          <Tab value="m">{DURATIONS.m}</Tab>
          <Tab value="y">{DURATIONS.y}</Tab>
        </TabList>
      </div>
      <div
        style={{
          height: `${height}px`,
          overflow: "hidden",
        }}
      >
        <LineChart
          key={selectedClientId ? `${selectedClientId}-${duration}` : duration}
          culture={window.navigator.language}
          enablePerfOptimization={true}
          data={data}
          hideLegend
          yMinValue={limits.min}
          yMaxValue={limits.max}
          width={width}
          height={height}
          yAxisTitle="Storage Usage (MB)"
          xAxisTitle={DURATIONS[duration]}
          {...(duration === "y" && {
            customDateTimeFormatter: (d) => d.getFullYear().toString(),
            // Restrict tick values to the number of xLabels returned
            tickValues: data.lineChartData?.[0].data.map((d) => d.x as Date),
          })}
          calloutProps={{
            styles: {
              calloutMain: {
                background: "var(--colorTooltipBackground)",
                color: "var(--colorTooltipForeground)",
              },
            },
          }}
          styles={lineChartStyles}
        />
      </div>
    </div>
  );
}
