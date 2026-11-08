import { Sparkline, ChartProps } from "@fluentui/react-charts-preview";
import { tokens } from "@fluentui/react-components";

import type { ProcessItem } from "../../api/urbackupserver";
import { format_speed_bpms_to_bps } from "../../utils/format";

export function ProcessSpeedChart(process: ProcessItem) {
  if (process.speed_bpms === 0 && process.past_speed_bpms.length === 0) {
    return "-";
  }

  const legend =
    process.speed_bpms > 0 ? format_speed_bpms_to_bps(process.speed_bpms) : "";

  const data: ChartProps = {
    chartTitle: "Speed chart",
    lineChartData: [
      {
        legend,
        color: tokens.colorBrandBackground,
        data: process.past_speed_bpms.map((d, i) => ({
          x: i + 1,
          y: d,
        })),
      },
    ],
  };

  return <Sparkline data={data} showLegend />;
}
