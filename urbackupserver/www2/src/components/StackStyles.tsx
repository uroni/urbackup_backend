import { makeStyles } from "@fluentui/react-components";

export const useStackStyles = makeStyles({
  stackVertical: {
    display: "flex",
    flexDirection: "column",
    flexWrap: "nowrap",
    width: "auto",
    minHeight: "100vh",
    boxSizing: "border-box",
    "> *": {
      textOverflow: "ellipsis",
    },
    "> :not(:first-child)": {
      marginTop: "0px",
    },
    "> *:not(.ms-StackItem)": {
      flexShrink: 1,
    },
  },
  stackHorizontal: {
    display: "flex",
    flexDirection: "row",
    flexWrap: "nowrap",
    width: "auto",
    height: "100%",
    boxSizing: "border-box",
    "> *": {
      textOverflow: "ellipsis",
    },
    "> :not(:first-child)": {
      marginTop: "0px",
    },
    "> *:not(.ms-StackItem)": {
      flexShrink: 1,
    },
  },
  item: {
    height: "auto",
    width: "auto",
    flexShrink: 1,
  },
  itemGrow: {
    height: "auto",
    width: "auto",
    flex: 1,
  },
  content: {
    padding: "10pt",
    maxWidth: "1200px",
    marginInline: "auto",
  },
});
