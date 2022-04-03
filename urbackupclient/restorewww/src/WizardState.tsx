export enum WizardState {
    Init = 1,
    SelectKeyboard,
    WaitForNetwork,
    SelectTimezone,
    ServerSearch,
    ConfigureServerConnectionDetails,
    WaitForConnection,
    LoginToServer,
    ConfigRestore,
    ConfigSpillSpace,
    ReviewRestore,
    Restoring
}

export interface BackupImage {
    letter: string;
    id: number;
    time_s: number;
    time_str: string;
    clientname: string;
    assoc: BackupImage[];
}

export interface LocalDisk {
    maj_min: string;
    model: string;
    path: string;
    size: string;
    type: string;
}

export interface SpillDisk {
    path: string;
    model: string;
    size: number;
    space: number;
    fstype: string;
    destructive: boolean;
}

export interface SpillSpace {
    live_medium: boolean,
    live_medium_space: number,
    disks: SpillDisk[]
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
    restoreOnlyMBR: boolean;
    restoreToPartition: boolean;
    spillSpace: SpillSpace;
    canRestoreSpill: boolean;
    canKeyboardConfig: boolean;
    timezoneArea: string;
    timezoneCity: string;
}

export interface WizardComponent {
    props: WizardStateProps;
    update: (props: WizardStateProps) => void;
}