export enum WizardState {
    Init = 1,
    SelectKeyboard,
    WaitForNetwork,
    ServerSearch,
    ConfigureServerConnectionDetails,
    WaitForConnection,
    LoginToServer,
    ConfigRestore,
    ReviewRestore,
    Restoring
}

export interface BackupImage {
    letter: string;
    id: number;
    time_s: number;
    time_str: string;
    clientname: string;
}

export interface LocalDisk {
    maj_min: string;
    model: string;
    path: string;
    size: string;
    type: string;
}
  
export interface WizardStateProps {
    state: WizardState;
    max_state: WizardState;
    serverFound: Boolean;
    internetServer: Boolean;
    serverUrl: string;
    serverAuthkey: string;
    serverProxy: string;
    username: string;
    password: string;
    restoreToDisk: LocalDisk;
    restoreImage: BackupImage;
    disableMenu: boolean;
    keyboardLayout: string;
}

export interface WizardComponent {
    props: WizardStateProps;
    update: (props: WizardStateProps) => void;
}