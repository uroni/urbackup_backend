import HeaderBar from "./HeaderBar";
import styles from "./Layout.module.css";

export function Layout({ children }: { children: React.ReactNode }) {
  return (
    <div className={styles.stackVertical}>
      <HeaderBar />
      <div className={styles.stackHorizontal}>{children}</div>
    </div>
  );
}

function Sidebar({ children }: { children: React.ReactNode }) {
  return <aside className={styles.sidebar}>{children}</aside>;
}

function Content({ children }: { children: React.ReactNode }) {
  return <main className={styles.content}>{children}</main>;
}

Layout.Sidebar = Sidebar;

Layout.Content = Content;
