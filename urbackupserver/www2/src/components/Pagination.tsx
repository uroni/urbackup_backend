import {
  Pagination as FUIPagination,
  IPaginationStyles,
} from "@fluentui/react-experiments";
import { Select, tokens } from "@fluentui/react-components";
import { registerIcons } from "@fluentui/react-experiments/lib/Styling";
import {
  ArrowNext20Filled,
  ArrowPrevious20Filled,
  ChevronLeft20Filled,
  ChevronRight20Filled,
} from "@fluentui/react-icons";
import { useState } from "react";

import { chunk } from "../utils/chunk";

// Register icons used in Pagination @fluentui/react-experiments. See https://github.com/microsoft/fluentui/wiki/Using-icons#registering-custom-icons.
registerIcons({
  icons: {
    CaretSolidLeft: <ChevronLeft20Filled />,
    CaretSolidRight: <ChevronRight20Filled />,
    Next: <ArrowNext20Filled />,
    Previous: <ArrowPrevious20Filled />,
  },
});

const PAGE_SIZES = [10, 25, 50, 100];

const paginationStyles: Partial<IPaginationStyles> = {
  root: {
    alignItems: "end",
    marginBlockStart: tokens.spacingHorizontalM,
  },
  pageNumber: {
    verticalAlign: "top",
    color: "currentColor",
  },
};

const pageSizeStyles = {
  display: "flex",
  alignItems: "center",
  gap: tokens.spacingHorizontalS,
};

export function usePagination<T>(
  data: T[],
  config: { itemsPerPage?: number } = {},
) {
  const [itemsPerPage, setItemsPerPage] = useState<number>(
    config.itemsPerPage ?? PAGE_SIZES[0],
  );

  const [page, setPage] = useState(0);

  const pageData = chunk(data, itemsPerPage);

  return { itemsPerPage, setItemsPerPage, pageData, page, setPage };
}

export function Pagination({
  page,
  setPage,
  pageCount,
  itemsPerPage,
  totalItemCount,
}: {
  page: number;
  setPage: React.Dispatch<React.SetStateAction<number>>;
  pageCount: number;
  itemsPerPage: number;
  totalItemCount: number;
}) {
  return (
    <FUIPagination
      selectedPageIndex={page}
      pageCount={pageCount}
      itemsPerPage={itemsPerPage}
      totalItemCount={totalItemCount}
      format={"buttons"}
      previousPageAriaLabel={"previous page"}
      nextPageAriaLabel={"next page"}
      firstPageAriaLabel={"first page"}
      lastPageAriaLabel={"last page"}
      pageAriaLabel={"page"}
      selectedAriaLabel={"selected"}
      onPageChange={(index) => setPage(index)}
      styles={paginationStyles}
    />
  );
}

export function PaginationItemsPerPageSelector({
  itemsPerPage,
  setItemsPerPage,
}: {
  itemsPerPage: number;
  setItemsPerPage: React.Dispatch<React.SetStateAction<number>>;
}) {
  return (
    <label style={pageSizeStyles}>
      Show
      <Select
        id="page-size"
        defaultValue={itemsPerPage}
        onChange={(_, data) => setItemsPerPage(+data.value)}
      >
        {PAGE_SIZES.map((size, id) => (
          <option key={id}>{size}</option>
        ))}
      </Select>
      entries
    </label>
  );
}
