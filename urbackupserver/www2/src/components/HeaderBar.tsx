import logoImage from "../assets/urbackup.png";
import { Avatar, Image } from "@fluentui/react-components";

export const HeaderBar = () => {
  return (
    <div className="repel">
      <div className="cluster gutter-s">
        <div>
          <Image src={logoImage} width="32" height="32" fit="contain" />
        </div>
        <div>UrBackup</div>
      </div>
      <Avatar aria-label="guest" />
    </div>
  );
};

export default HeaderBar;
