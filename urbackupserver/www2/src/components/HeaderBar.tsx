import logoImage from "../assets/urbackup.png";
import {
  Avatar,
  Image,
  Menu,
  MenuButton,
  MenuItem,
  MenuList,
  MenuPopover,
  MenuTrigger,
} from "@fluentui/react-components";
import { PowerRegular } from "@fluentui/react-icons";
import { useNavigate } from "react-router-dom";

import { useLogout } from "../pages/Login";
import styles from "./Layout.module.css";

export const HeaderBar = () => {
  const navigate = useNavigate();
  const logout = useLogout();

  const handleLogout = () => {
    logout.mutate(undefined, {
      onSettled: () => void navigate("/login", { replace: true }),
    });
  };

  return (
    <div className={`${styles.header} repel`}>
      <div className="cluster gutter-s">
        <Image src={logoImage} width="32" height="32" fit="contain" />
        <div>UrBackup</div>
      </div>
      <Menu positioning={{ autoSize: true }}>
        <MenuTrigger disableButtonEnhancement>
          <MenuButton appearance="transparent">
            <Avatar aria-label="guest" />
          </MenuButton>
        </MenuTrigger>
        <MenuPopover>
          <MenuList>
            <MenuItem
              icon={<PowerRegular />}
              disabled={logout.isPending}
              onClick={handleLogout}
            >
              Logout
            </MenuItem>
          </MenuList>
        </MenuPopover>
      </Menu>
    </div>
  );
};

export default HeaderBar;
