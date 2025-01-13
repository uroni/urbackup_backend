import { useState } from "react";
import { useSuspenseQuery } from "@tanstack/react-query";
import {
  DonutChart,
  ChartProps,
  getColorFromToken,
  DataVizPalette,
} from "@fluentui/react-charts-preview";
import { tokens } from "@fluentui/react-components";

import { format_size } from "../../utils/format";
import { urbackupServer } from "../../App";

const colors = Object.entries(DataVizPalette)
  .filter(([k]) => k.includes("color"))
  .map(([, v]) => getColorFromToken(v));

export function StorageAllocation() {
  const clientsStorageUsageResult = useSuspenseQuery({
    queryKey: ["client-storage-usage"],
    queryFn: () => urbackupServer.getPiegraphData(),
  });

  const clientsStorageUsage = clientsStorageUsageResult.data;

  const data: ChartProps = {
    chartTitle: "Storage Allocation",
    chartData: clientsStorageUsage
      .sort((a, b) => a.data - b.data)
      .map((d, i) => ({
        legend: d.label,
        data: d.data,
        yAxisCalloutData: format_size(d.data),
        // Don't use colours from palette if no data, to make legends
        // easier to parse.
        color: d.data ? colors[i] : tokens.colorNeutralForegroundDisabled,
      })),
  };

  const [showChart, setShowChart] = useState(false);

  const isStorageInUse = data.chartData?.some((d) => d.data !== 0);

  if (!isStorageInUse) {
    return <span>No storage in use by clients</span>;
  }

  return (
    <div
      style={{
        visibility: showChart ? "initial" : "hidden",
      }}
    >
      <DonutChart
        onResize={() => {
          /**
           * Required to set the chart visible after it resizes itself
           * on initial render according to container dimensions. Without the check,
           * the chart flashes into the correct size after initial render.
           */
          if (!showChart) {
            setShowChart(true);
          }
        }}
        data={data}
        innerRadius={100}
        legendProps={{
          enabledWrapLines: true,
          allowFocusOnLegends: true,
          styles: {
            rect: "donut-chart__legend-rect",
          },
        }}
        hideLabels={false}
        showLabelsInPercent
        styles={{
          chart: "donut-chart",
          legendContainer: "donut-chart__legend",
        }}
      />
    </div>
  );
}
