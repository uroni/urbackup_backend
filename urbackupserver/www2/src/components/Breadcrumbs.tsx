/**
 * Adapted from https://react.fluentui.dev/?path=/docs/components-breadcrumb--docs#breadcrumb-with-tooltip
 */

import React, { Fragment } from "react";
import {
  Breadcrumb as FUIBreadcrumb,
  BreadcrumbItem,
  BreadcrumbButton,
  BreadcrumbDivider,
  partitionBreadcrumbItems,
  truncateBreadcrumbLongName,
  isTruncatableBreadcrumbContent,
  Tooltip,
  useIsOverflowItemVisible,
  Menu,
  MenuTrigger,
  useOverflowMenu,
  MenuPopover,
  MenuList,
  Button,
  MenuItemLink,
  tokens,
} from "@fluentui/react-components";
import {
  MoreHorizontalRegular,
  MoreHorizontalFilled,
  bundleIcon,
} from "@fluentui/react-icons";
import type { PartitionBreadcrumbItems } from "@fluentui/react-components";

export type BreadcrumbItem = {
  key: string | number;
  text: string;
  itemProps: {
    href: string;
  };
};

const MAX_TEXT_LENGTH = 30;
const MAX_DISPLAYED_ITEMS = 6;

export const BASE_HREF = `${window.location.origin}/#` as const;

const MoreHorizontal = bundleIcon(MoreHorizontalFilled, MoreHorizontalRegular);

const styles = {
  breadcrumbText: {
    fontWeight: tokens.fontWeightRegular,
    margin: 0,
  },
  breadcrumbTextLast: {
    fontWeight: tokens.fontWeightBold,
    margin: 0,
  },
  tooltip: {
    whiteSpace: "nowrap",
    overflow: "hidden",
    textOverflow: "ellipsis",
  },
};

function renderItem(
  entry: BreadcrumbItem,
  isLastItem: boolean,
  wrapper?: React.ElementType<{ children: React.ReactNode }>,
) {
  const ButtonWrapper = wrapper ?? "span";

  return (
    <Fragment key={`item-${entry.key}`}>
      {isTruncatableBreadcrumbContent(entry.text, MAX_TEXT_LENGTH) ? (
        <BreadcrumbItem>
          <Tooltip withArrow content={entry.text} relationship="label">
            <BreadcrumbButton current={isLastItem} href={entry.itemProps?.href}>
              <ButtonWrapper
                style={
                  isLastItem ? styles.breadcrumbTextLast : styles.breadcrumbText
                }
              >
                {truncateBreadcrumbLongName(entry.text)}
              </ButtonWrapper>
            </BreadcrumbButton>
          </Tooltip>
        </BreadcrumbItem>
      ) : (
        <BreadcrumbItem>
          <BreadcrumbButton current={isLastItem} href={entry.itemProps?.href}>
            <ButtonWrapper
              style={
                isLastItem ? styles.breadcrumbTextLast : styles.breadcrumbText
              }
            >
              {entry.text}
            </ButtonWrapper>
          </BreadcrumbButton>
        </BreadcrumbItem>
      )}

      {!isLastItem && <BreadcrumbDivider />}
    </Fragment>
  );
}

function BreadcrumbMenuItem({ item }: { item: BreadcrumbItem }) {
  const isVisible = useIsOverflowItemVisible(item.key.toString());

  if (isVisible) {
    return null;
  }

  return <MenuItemLink href={item.itemProps?.href}>{item.text}</MenuItemLink>;
}

function MenuWithTooltip({
  overflowItems,
  startDisplayedItems,
  endDisplayedItems,
}: PartitionBreadcrumbItems<BreadcrumbItem>) {
  const { ref, isOverflowing, overflowCount } =
    useOverflowMenu<HTMLButtonElement>();

  if (!isOverflowing && overflowItems && overflowItems.length === 0) {
    return null;
  }

  const overflowItemsCount = overflowItems
    ? overflowItems.length + overflowCount
    : overflowCount;

  const tooltipContent = {
    children:
      overflowItemsCount > 3
        ? `${overflowItemsCount} items`
        : getTooltipContent(overflowItems),
    style: styles.tooltip,
  };

  return (
    <Menu hasIcons>
      <MenuTrigger disableButtonEnhancement>
        <Tooltip withArrow content={tooltipContent} relationship="label">
          <Button
            id="menu"
            appearance="subtle"
            ref={ref}
            icon={<MoreHorizontal />}
            aria-label={`${overflowItemsCount} more items`}
            role="button"
          />
        </Tooltip>
      </MenuTrigger>
      <MenuPopover>
        <MenuList>
          {isOverflowing &&
            startDisplayedItems.map((item: BreadcrumbItem) => (
              <BreadcrumbMenuItem item={item} key={item.key} />
            ))}
          {overflowItems &&
            overflowItems.map((item: BreadcrumbItem) => (
              <BreadcrumbMenuItem item={item} key={item.key} />
            ))}
          {isOverflowing &&
            endDisplayedItems &&
            endDisplayedItems.map((item: BreadcrumbItem) => (
              <BreadcrumbMenuItem item={item} key={item.key} />
            ))}
        </MenuList>
      </MenuPopover>
    </Menu>
  );
}

const getTooltipContent = (
  breadcrumbItems: readonly BreadcrumbItem[] | undefined,
) => {
  if (!breadcrumbItems) {
    return "";
  }
  return breadcrumbItems.reduce(
    (acc, initialValue, _idx, arr) => {
      return (
        <>
          {acc}
          {arr[0].text !== initialValue.text && " > "}
          {initialValue.text}
        </>
      );
    },
    <Fragment />,
  );
};

export function Breadcrumbs({
  items,
  wrapper,
}: {
  items: BreadcrumbItem[];
  wrapper?: React.ElementType<{ children: React.ReactNode }>;
}) {
  const {
    startDisplayedItems,
    overflowItems,
    endDisplayedItems,
  }: PartitionBreadcrumbItems<BreadcrumbItem> = partitionBreadcrumbItems({
    items,
    maxDisplayedItems: MAX_DISPLAYED_ITEMS,
  });

  return (
    <FUIBreadcrumb aria-label="breadcrumb">
      {startDisplayedItems.map((item) => renderItem(item, false, wrapper))}

      {overflowItems && (
        <>
          <MenuWithTooltip
            overflowItems={overflowItems}
            startDisplayedItems={startDisplayedItems}
            endDisplayedItems={endDisplayedItems}
          />
          <BreadcrumbDivider />
        </>
      )}

      {endDisplayedItems &&
        endDisplayedItems.map((item, index, array) =>
          renderItem(item, index === array.length - 1, wrapper),
        )}
    </FUIBreadcrumb>
  );
}
