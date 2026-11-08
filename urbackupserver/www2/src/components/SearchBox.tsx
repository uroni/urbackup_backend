import {
  Field,
  SearchBox as FUISearchBox,
  tokens,
} from "@fluentui/react-components";
import { useState } from "react";

const styles: Record<string, React.CSSProperties> = {
  search: {
    display: "flex",
    alignItems: "center",
    gap: tokens.spacingHorizontalS,
  },
  searchBox: {
    width: "28ch",
  },
};

export function useFilteredBySearch<T>(
  data: T[],
  filterFn: (d: T, search: string) => boolean,
) {
  const [search, setSearch] = useState("");

  const filteredItems = data.filter((d) => filterFn(d, search));

  return {
    search,
    setSearch,
    filteredItems,
  };
}

export function SearchBox({
  onSearch,
}: {
  onSearch: (search: string) => void;
}) {
  return (
    <Field label="Search" style={styles.search}>
      <FUISearchBox
        autoComplete="off"
        onChange={(_, data) => {
          const search = data.value.toLowerCase();

          onSearch(search);
        }}
        style={styles.searchBox}
      />
    </Field>
  );
}

export function filterBySearch(
  search: string,
  searchableFields: Record<string, string>,
): boolean {
  if (search.length) {
    // Find matching search term in data values
    const match = Object.values(searchableFields).some((v) =>
      String(v).toLowerCase().includes(search),
    );

    return match;
  }

  return true;
}
