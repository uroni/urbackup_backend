import logoImage from "../assets/urbackup.png";
import { Avatar, Image } from "@fluentui/react-components";

import styles from "./Layout.module.css";

export const HeaderBar = () => {
  return (
    <div className={`${styles.header} repel`}>
      <div className="cluster gutter-s">
        <Image src={logoImage} width="32" height="32" fit="contain" />
        <div>UrBackup</div>
      </div>
      <Avatar aria-label="guest" />
    </div>
  );
};

export default HeaderBar;
