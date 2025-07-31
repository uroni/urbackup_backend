import logoImage from "../assets/urbackup.png";
import { Avatar, Image } from "@fluentui/react-components";

export const HeaderBar = () => {
  return (
    <div className="repel">
      <div className="cluster">
        <div style={{ padding: "5px" }}>
          <Image src={logoImage} width="40" height="40" fit="contain" />
        </div>
        <div>UrBackup</div>
      </div>
      <Avatar aria-label="guest" />
    </div>
  );
};

export default HeaderBar;
