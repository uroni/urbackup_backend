import { Subtitle1, tokens } from "@fluentui/react-components";
import {
  getColorFromToken,
  DataVizPalette,
} from "@fluentui/react-charts-preview";

import { format_size } from "../../utils/format";
import { UsageStats } from "../../api/urbackupserver";

const CHART_HEIGHT = 12;
const CHART_GAP = 0.6;
const FILES_COLOR = getColorFromToken(DataVizPalette.color1);
const IMAGES_COLOR = getColorFromToken(DataVizPalette.color4);

const styles: Record<string, React.CSSProperties> = {
  root: {
    "--flow-space": tokens.spacingVerticalS,
  } as React.CSSProperties,
  legendList: {
    "--flow-space": tokens.spacingVerticalS,
  } as React.CSSProperties,
  legendItem: {
    "--gutter": tokens.spacingHorizontalS,
  } as React.CSSProperties,
  legendShape: {
    width: "12px",
    aspectRatio: 1,
    borderRadius: tokens.borderRadiusSmall,
  },
  legendValue: {
    marginInlineStart: "auto",
  },
};

export function TotalStorageUsage({ usage }: { usage: UsageStats["usage"] }) {
  const totalFilesUsage = usage.map((d) => d.files).reduce(sum, 0);
  const totalImagesUsage = usage.map((d) => d.images).reduce(sum, 0);
  const totalUsage = usage.map((d) => d.used).reduce(sum, 0);

  return (
    <div className="flow">
      <div className="flow" style={styles.root}>
        <span>
          <Subtitle1>{format_size(totalUsage)}</Subtitle1> used
        </span>
        <svg width="100%" height={CHART_HEIGHT}>
          <g>
            <ChartRect
              x="0"
              width={`${(totalFilesUsage / totalUsage) * 100}%`}
              fill={FILES_COLOR}
            />
            <ChartRect
              x={`${(totalFilesUsage / totalUsage) * 100 + CHART_GAP}%`}
              width={`${Math.max(0, (totalImagesUsage / totalUsage) * 100 - CHART_GAP)}%`}
              fill={IMAGES_COLOR}
            />
          </g>
        </svg>
      </div>
      <ul className="flow" style={styles.legendList}>
        <li className="cluster" style={styles.legendItem}>
          <div
            style={{
              ...styles.legendShape,
              background: FILES_COLOR,
            }}
          ></div>
          <span>Files</span>
          <span style={styles.legendValue}>{format_size(totalFilesUsage)}</span>
        </li>
        <li className="cluster" style={styles.legendItem}>
          <div
            style={{
              ...styles.legendShape,
              background: IMAGES_COLOR,
            }}
          ></div>
          <span>Images</span>
          <span style={styles.legendValue}>
            {format_size(totalImagesUsage)}
          </span>
        </li>
      </ul>
    </div>
  );
}

function ChartRect(props: React.SVGAttributes<SVGRectElement>) {
  return (
    <rect y="0" role="img" rx="2" ry="2" height={CHART_HEIGHT} {...props} />
  );
}

function sum(acc: number, curr: number) {
  return acc + curr;
}
