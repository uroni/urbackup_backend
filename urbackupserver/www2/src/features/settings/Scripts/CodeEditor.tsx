import { useEffect, useRef } from "react";
import { basicSetup, EditorView } from "codemirror";
import { indentWithTab } from "@codemirror/commands";
import { EditorState } from "@codemirror/state";
import { keymap } from "@codemirror/view";
import {
  HighlightStyle,
  StreamLanguage,
  syntaxHighlighting,
} from "@codemirror/language";
import { lua } from "@codemirror/legacy-modes/mode/lua";
import { tags } from "@lezer/highlight";

import styles from "./Scripts.module.css";

// Fluent palette tokens, so the colors follow the light and dark theme
const highlightStyle = HighlightStyle.define([
  { tag: tags.keyword, color: "var(--colorPaletteBerryForeground2)" },
  { tag: tags.string, color: "var(--colorPaletteGreenForeground2)" },
  { tag: tags.number, color: "var(--colorPaletteDarkOrangeForeground2)" },
  { tag: tags.comment, color: "var(--colorNeutralForeground3)" },
  { tag: tags.variableName, color: "var(--colorNeutralForeground1)" },
  {
    tag: [tags.standard(tags.variableName), tags.special(tags.variableName)],
    color: "var(--colorPaletteBlueForeground2)",
  },
]);

const theme = EditorView.theme({
  "&": {
    height: "100%",
    color: "var(--colorNeutralForeground1)",
    backgroundColor: "var(--colorNeutralBackground1)",
  },
  ".cm-scroller": { fontFamily: "var(--fontFamilyMonospace)" },
  ".cm-gutters": {
    color: "var(--colorNeutralForeground3)",
    backgroundColor: "var(--colorNeutralBackground2)",
    borderRight: "1px solid var(--colorNeutralStroke2)",
  },
  ".cm-activeLine, .cm-activeLineGutter": {
    backgroundColor: "var(--colorSubtleBackgroundHover)",
  },
  "&.cm-focused .cm-cursor": {
    borderLeftColor: "var(--colorNeutralForeground1)",
  },
});

/**
 * Editor with Lua syntax highlighting. `value` is only read initially
 */
export function CodeEditor({
  value,
  onChange,
  readOnly = false,
  rows,
  label,
}: {
  value: string;
  onChange: (value: string) => void;
  readOnly?: boolean;
  rows: number;
  label: string;
}) {
  const parent = useRef<HTMLDivElement>(null);
  const changed = useRef(onChange);
  changed.current = onChange;

  useEffect(() => {
    const view = new EditorView({
      parent: parent.current!,
      state: EditorState.create({
        doc: value,
        extensions: [
          basicSetup,
          // Escape and then Tab still moves the focus out of the editor
          keymap.of([indentWithTab]),
          StreamLanguage.define(lua),
          syntaxHighlighting(highlightStyle),
          theme,
          EditorState.readOnly.of(readOnly),
          EditorView.contentAttributes.of({ "aria-label": label }),
          EditorView.updateListener.of((u) => {
            if (u.docChanged) {
              changed.current(u.state.doc.toString());
            }
          }),
        ],
      }),
    });

    return () => view.destroy();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [readOnly]);

  return (
    <div
      ref={parent}
      className={styles.editor}
      style={{ height: `${rows * 1.5}em` }}
    />
  );
}
