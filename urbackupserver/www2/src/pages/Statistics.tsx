import { Suspense, useEffect, useRef, useState } from "react";
import { Spinner } from "@fluentui/react-components";

import { StorageUsage } from "../features/statistics/StorageUsage";

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
          <StorageUsage width={width / 2} height={300} />
        </div>
      </div>
    </Suspense>
  );
};
