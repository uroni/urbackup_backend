package PVE::BackupProvider::Plugin::UrBackup;

# Proxmox VE backup provider for UrBackup.
#
# A guest is backed up by the UrBackup client on the Proxmox VE host as its virtual client
# "pve-<vmid>". A VM:
#  - each disk's backup export (NBD) is connected to a /dev/nbdX node, which gets the stable name
#    /run/urbackup-pve/<vmid>/<device> (the nbd node changes between backups),
#  - the client reports these disks as the volumes of the virtual client and asks the server to
#    image all of them (urbackup/data/client_defaults_pve-<vmid>.cfg),
#  - for each disk there is a zip file with the guest configuration and the size of the disk
#    (urbackup/data/disk_pve-<vmid>_<device>.zip). The client gives it to the server in place of
#    the partition table of a volume's disk, which makes the server store the image as an image
#    of a whole disk and keep the zip file with it,
#  - the snapshot script of the virtual client hands the disks to the client as they are (the
#    export already is a consistent snapshot),
#  - "urbackupclientctl start --image -v pve-<vmid>" asks the server to start the image backups;
#    the plugin waits until the client has finished the one of each disk,
#  - with the bitmaps of Proxmox VE, only the parts of the disks that have changed since the last
#    backup are read (see bitmap_state_file()).
#
# A VM is restored from the images of one backup job: the zip file of an image has the guest
# configuration, and the client downloads the images to files that Proxmox VE imports.
#
# A container is a file backup of the virtual client:
#  - its file system is mounted at /run/urbackup-pve/<vmid>/fs/rootfs, with the user and group
#    IDs the container sees (an unprivileged container has other ones on the host), and for
#    the host (vzdump mounts the file system where only it sees it),
#  - /run/urbackup-pve/<vmid>/config has its configuration,
#  - the virtual client backs up these two directories as "pve-<vmid>-rootfs" and
#    "pve-<vmid>-config" (urbackup/data/client_defaults_pve-<vmid>.cfg),
#  - "urbackupclientctl start -v pve-<vmid>" asks the server to start the file backup; the plugin
#    waits until the server has it. That happens in backup_container_prepare().
#
# For a restore, the client restores the file system to another directory, from which Proxmox VE
# copies the files to the new container.

use strict;
use warnings;

use Fcntl qw(S_ISSOCK);
use File::Path qw(make_path remove_tree);
use File::stat ();
use File::Temp ();
use IO::Compress::Zip qw($ZipError);
use IO::File;
use JSON;

use PVE::Tools qw(file_get_contents file_read_firstline file_set_contents run_command);

use base qw(PVE::BackupProvider::Plugin::Base);

my $RUN_DIR = '/run/urbackup-pve';
my $SNAPSHOT_SCRIPT_DIR = '/usr/share/urbackup-pve';
my $LOCK_FILE = '/run/lock/urbackup-pve.lock';
# Seconds the server gets to finish the images after the client has sent the data
my $SERVER_FINISH_TIMEOUT = 15 * 60;
# Seconds the server gets to take the directories of a container from the client
my $SERVER_SETTINGS_TIMEOUT = 15 * 60;
# Days after which what a restore left in the restore directory is removed (it does that if it
# is killed)
my $RESTORE_LEFTOVER_DAYS = 7;
# What vzdump leaves out of the backup of a container unless it is told otherwise, and the
# directories the root user of an unprivileged container cannot read
my $CONTAINER_EXCLUDES = ['/tmp/?*', '/var/tmp/?*', '/var/run/?*.pid'];
my $CONTAINER_EXCLUDES_UNREADABLE = ['/lost+found', '/*/lost+found'];
# The mounts of the host, and how to run a command with them
my $HOST_MOUNT_NS = '/proc/1/ns/mnt';
my $IN_HOST_MOUNT_NS = ['nsenter', "--mount=$HOST_MOUNT_NS", '--'];
# open_tree(2) and move_mount(2), which have the same numbers on all architectures
my $SYS_OPEN_TREE = 428;
my $SYS_MOVE_MOUNT = 429;
my $AT_FDCWD = -100;
my $AT_RECURSIVE = 0x8000;
my $OPEN_TREE_CLONE = 1;
my $MOVE_MOUNT_F_EMPTY_PATH = 4;

# Private helpers

my sub log_info {
    my ($self, $message) = @_;
    $self->{'log-function'}->('info', $message);
}

my sub log_warning {
    my ($self, $message) = @_;
    $self->{'log-function'}->('warn', $message);
}

my sub log_error {
    my ($self, $message) = @_;
    $self->{'log-function'}->('err', $message);
}

my sub option {
    my ($self, $name) = @_;
    return $self->{'storage-plugin'}->get_option($self->{scfg}, $name);
}

my sub virtual_client {
    my ($vmid) = @_;
    return "pve-$vmid";
}

# The ID of a guest, untainted: vzdump runs with -T, and the ID is part of file names
my sub guest_id {
    my ($vmid) = @_;
    ($vmid) = $vmid =~ m/^(\d+)$/ or die "unexpected guest ID\n";
    return $vmid;
}

# Reads a key=value file of the UrBackup client
my sub read_kv_file {
    my ($fn) = @_;
    my $ret = {};
    return $ret if !-e $fn;
    for my $line (split(/\n/, file_get_contents($fn))) {
        next if $line !~ m/^\s*([^=\s]+)\s*=(.*)$/;
        $ret->{$1} = $2;
    }
    return $ret;
}

my sub write_kv_file {
    my ($fn, $values) = @_;
    my $data = '';
    $data .= "$_=$values->{$_}\n" for sort keys $values->%*;
    file_set_contents($fn, $data, 0600);
}

# Connects an NBD export to a free /dev/nbdX node (like the Proxmox VE example plugins). The
# node is reserved for a minute to avoid races between allocation and use.
my sub bind_next_free_dev_nbd_node {
    my ($self, $options) = @_;

    my $line = file_read_firstline("/sys/module/nbd/parameters/nbds_max")
        or die "could not read 'nbds_max' parameter of the 'nbd' kernel module\n";
    my ($nbds_max) = ($line =~ m/(\d+)/)
        or die "could not determine 'nbds_max' parameter of the 'nbd' kernel module\n";

    my $filename = "/run/qemu-server/reserved-dev-nbd-nodes";

    my $code = sub {
        my $expiretime = 60;
        my $ctime = time();

        my $used = {};
        my $latest = [0, 0];

        if (my $fh = IO::File->new($filename, "r")) {
            while (my $line = <$fh>) {
                if ($line =~ m/^(\d+)\s(\d+)$/) {
                    my ($n, $timestamp) = ($1, $2);
                    $latest = [$n, $timestamp] if $latest->[1] <= $timestamp;
                    $used->{$n} = $timestamp if ($timestamp + $expiretime) > $ctime;
                }
            }
        }

        my $new_n;
        for (my $count = 0; $count < $nbds_max; $count++) {
            my $n = ($latest->[0] + $count) % $nbds_max;
            my $block_device = "/dev/nbd${n}";
            next if $used->{$n};
            next if !-e $block_device;

            my $st = File::stat::stat("/run/lock/qemu-nbd-nbd${n}");
            next if defined($st) && S_ISSOCK($st->mode) && $st->uid == 0; # in use

            my $socket_error = 0;
            eval {
                my $errfunc = sub {
                    my ($line) = @_;
                    $socket_error = 1 if $line =~ m/^qemu-nbd: Failed to set NBD socket$/;
                    log_warning($self, $line);
                };
                run_command(["qemu-nbd", "-c", $block_device, $options->@*], errfunc => $errfunc);
            };
            if (my $err = $@) {
                die $err if !$socket_error;
                log_warning($self, "unable to bind $block_device - trying next one");
                next;
            }
            $used->{$n} = $ctime;
            $new_n = $n;
            last;
        }

        my $data = "";
        $data .= "$_ $used->{$_}\n" for keys $used->%*;
        file_set_contents($filename, $data);

        return defined($new_n) ? "/dev/nbd${new_n}" : undef;
    };

    make_path('/run/lock/qemu-server');
    my $block_device =
        PVE::Tools::lock_file('/run/lock/qemu-server/reserved-dev-nbd-nodes.lock', 10, $code);
    die $@ if $@;
    die "unable to find a free /dev/nbdX block device node\n" if !$block_device;

    return $block_device;
}

# Removes the directory of a guest's backup in /run. The file system of a container is mounted
# there (read-only) during its backup: for the host, and before that for vzdump.
my sub remove_run_dir {
    my ($self, $vmid) = @_;

    my $dir = "$RUN_DIR/$vmid";
    return if !-d $dir;

    my $rootfs = $self->{'storage-plugin'}->container_rootfs_dir($vmid);
    for my $mounts (['self'], [1, $IN_HOST_MOUNT_NS->@*]) {
        my ($pid, @enter) = $mounts->@*;
        my $mounted = sub {
            return grep { m!^\Q$dir\E(?:/|$)! }
                map { (split(/ /, $_))[4] // () }
                split(/\n/, file_get_contents("/proc/$pid/mountinfo"));
        };
        next if !$mounted->();

        eval { run_command([@enter, 'umount', '-R', $rootfs], errfunc => sub { }) };
        # (e.g. the client still reads files of a backup that failed)
        eval { run_command([@enter, 'umount', '-R', '-l', $rootfs]) } if $mounted->();
        if ($mounted->()) {
            log_warning($self, "unable to unmount the file system of the container in $dir - $@");
            return;
        }
    }

    remove_tree($dir);
}

# Moves what is mounted at a path here to the same path of the host.
#
# vzdump mounts the file system of a container with mounts of its own (a mount namespace), for
# the host to not see it. The client is not part of that: it only sees what the host has
# mounted.
my sub move_mounts_to_host {
    my ($path) = @_;

    return if readlink('/proc/self/ns/mnt') eq readlink($HOST_MOUNT_NS);

    PVE::Tools::run_fork(sub {
        my $tree = syscall($SYS_OPEN_TREE, $AT_FDCWD, $path, $OPEN_TREE_CLONE | $AT_RECURSIVE);
        die "unable to copy the mounts at $path - $!\n" if $tree < 0;

        open(my $host_ns, '<', $HOST_MOUNT_NS) or die "unable to open $HOST_MOUNT_NS - $!\n";
        PVE::Tools::setns(fileno($host_ns), PVE::Tools::CLONE_NEWNS)
            or die "unable to use the mounts of the host - $!\n";

        my $empty = '';
        syscall($SYS_MOVE_MOUNT, $tree, $empty, $AT_FDCWD, $path, $MOVE_MOUNT_F_EMPTY_PATH) == 0
            or die "unable to mount $path for the host - $!\n";
        return;
    });

    run_command(['umount', '-R', $path]);
}

my sub cleanup_backup {
    my ($self, $vmid) = @_;

    for my $node (($self->{$vmid}->{'nbd-nodes'} // [])->@*) {
        eval { run_command(["qemu-nbd", "-d", $node], outfunc => sub {}) };
        log_warning($self, "unable to disconnect $node - $@") if $@;
    }
    $self->{$vmid}->{'nbd-nodes'} = [];

    remove_run_dir($self, $vmid);

    # (untainted: vzdump runs with -T)
    my $client_dir = option($self, 'client-dir');
    for my $zip (glob("$client_dir/data/disk_pve-${vmid}_*.zip")) {
        unlink($1) if $zip =~ m!^(.*/disk_pve-\d+_[\w.-]+\.zip)$!;
    }
}

# The file the client gives to the server as information about a disk of the guest
my sub disk_info_file {
    my ($self, $vmid, $device_name) = @_;
    return option($self, 'client-dir') . "/data/disk_pve-${vmid}_${device_name}.zip";
}

my sub write_disk_info {
    my ($self, $vmid, $device_name, $size, $devices, $guest_config, $firewall_config) = @_;

    my $fn = disk_info_file($self, $vmid, $device_name);
    # The backup job the image belongs to and the disks it backs up: the storage plugin finds
    # the images of a backup on the server with it
    my $disk = {
        vmid => int($vmid),
        device => $device_name,
        size => int($size),
        'backup-time' => int($self->{$vmid}->{'backup-time'}),
        devices => $devices,
    };

    my $zip = IO::Compress::Zip->new($fn, Name => 'disk.json')
        or die "unable to create $fn - $ZipError\n";
    $zip->print(JSON->new->canonical->encode($disk));
    $zip->newStream(Name => 'qemu-server.conf');
    $zip->print($guest_config);
    if (defined($firewall_config)) {
        $zip->newStream(Name => 'firewall.fw');
        $zip->print($firewall_config);
    }
    $zip->close() or die "unable to write $fn - $ZipError\n";
    chmod(0600, $fn);
}

# Sets up the client so that "pve-<vmid>" is a virtual client with the given default settings
# on the server. $image_backups: of volumes that the snapshot scripts of the plugin hand to it.
my sub configure_client {
    my ($self, $vmid, $settings, $image_backups) = @_;

    my $client_dir = option($self, 'client-dir');
    my $conf_dir = option($self, 'client-conf-dir');
    my $vc = virtual_client($vmid);

    PVE::Tools::lock_file($LOCK_FILE, 30, sub {
        # Main client: the virtual clients, and let the server ask for them regularly
        my $defaults_fn = "$client_dir/data/client_defaults.cfg";
        my $defaults = read_kv_file($defaults_fn);
        my @vcs = grep { $_ ne '' } split(/\|/, $defaults->{virtual_clients_add} // '');
        push @vcs, $vc if !grep { $_ eq $vc } @vcs;
        $defaults->{virtual_clients_add} = join('|', @vcs);
        $defaults->{update_capa_interval} //= 60000;
        write_kv_file($defaults_fn, $defaults);

        write_kv_file("$client_dir/data/client_defaults_${vc}.cfg", {
            # No scheduled backups: the guest can only be read while vzdump backs it up, and
            # without directories of its own the virtual client would back up those of the host
            update_freq_image_incr => -7 * 24 * 3600,
            update_freq_image_full => -60 * 24 * 3600,
            update_freq_incr => -5 * 3600,
            update_freq_full => -30 * 24 * 3600,
            $settings->%*,
        });

        # Snapshot scripts of the virtual client (the client can do image backups of a virtual
        # client that has them)
        my $snapshot_fn = "$conf_dir/snapshot.cfg";
        my $snapshot = read_kv_file($snapshot_fn);
        for my $script (qw(create_volume_snapshot remove_volume_snapshot)) {
            if ($image_backups) {
                $snapshot->{"${vc}_$script"} = "$SNAPSHOT_SCRIPT_DIR/$script";
            } else {
                delete $snapshot->{"${vc}_$script"};
            }
        }
        make_path($conf_dir);
        write_kv_file($snapshot_fn, $snapshot);
    });
    die $@ if $@;
}

# The settings of the virtual client of a VM: image backups of all its volumes (the disks). The
# server keeps the capabilities of each virtual client separately, so it needs to ask it
# regularly (its volumes change when disks are added or removed). No snapshot group: the exports
# already are a snapshot of all disks at the same point in time, and the server would only back
# up one volume of a group the client does not snapshot together. Incremental image backups are
# of the changes since the last image backup, not since the last full one: that is what the
# bitmaps of Proxmox VE are of (see bitmap_state_file()).
my sub vm_client_settings {
    my ($volumes) = @_;

    return {
        image_letters => 'ALL',
        image_snapshot_groups_def => '',
        all_volumes => join(';', $volumes->@*),
        update_capa_interval => 60000,
        local_incr_image_style => 'to-last',
        internet_incr_image_style => 'to-last',
    };
}

# The settings of the virtual client of a container: file backups of its file system and its
# configuration. Symbolic links are backed up as they are (those of a container lead to other
# files on the host). The server takes the settings when it asks for the capabilities of the
# virtual client, so it needs to do that regularly: a VM can become a container.
my sub container_client_settings {
    my ($self, $vmid) = @_;

    my $plugin = $self->{'storage-plugin'};
    my $rootfs = $plugin->container_rootfs_dir($vmid);
    my $rootfs_name = $plugin->container_rootfs_name($vmid);
    my $config_dir = $plugin->container_config_dir($vmid);
    my $config_name = $plugin->container_config_name($vmid);

    return {
        default_dirs => "$rootfs|$rootfs_name/required,share_hashes"
            . ";$config_dir|$config_name/required",
        exclude_files => join(';',
            map { "$rootfs$_" } $CONTAINER_EXCLUDES->@*, $CONTAINER_EXCLUDES_UNREADABLE->@*),
        update_capa_interval => 60000,
    };
}

# Whether the virtual client of a container backs up the directories of the container, and only
# them. The client has the directories from the server, which takes them from the settings
# above when it does not have directories for the virtual client yet. Until then, a file backup
# would be one of the directories of the host.
my sub client_has_container_dirs {
    my ($self, $vmid) = @_;

    my $plugin = $self->{'storage-plugin'};
    my ($rc, $out) = $plugin->clientctl($self->{scfg}, ['list-backupdirs', '--raw']);
    my $list = $rc == 0 ? eval { decode_json($out) } : undef;
    return 0 if ref($list) ne 'HASH' || ref($list->{dirs}) ne 'ARRAY';

    my $vc = virtual_client($vmid);
    my $dirs = {
        map { ($_->{path} // '') => ($_->{name} // '') }
        grep { ($_->{virtual_client} // '') eq $vc } $list->{dirs}->@*
    };

    return scalar(keys $dirs->%*) == 2
        && ($dirs->{ $plugin->container_rootfs_dir($vmid) } // '')
            eq $plugin->container_rootfs_name($vmid)
        && ($dirs->{ $plugin->container_config_dir($vmid) } // '')
            eq $plugin->container_config_name($vmid);
}

# Whether the next image backup of the guest should be a full one. The server's own schedule is
# off for the virtual clients (the disks can only be read during a backup job), so the plugin
# decides.
my sub full_image_due {
    my ($self, $vmid) = @_;

    my $interval_days = option($self, 'full-image-interval');
    return 0 if !$interval_days;

    my $state_dir = $self->{'storage-plugin'}->state_dir();
    my $last_full = file_read_firstline("$state_dir/$vmid.last-full") // 0;
    return time() - $last_full >= $interval_days * 24 * 3600;
}

my sub record_full_image {
    my ($self, $vmid, $time) = @_;
    my $state_dir = $self->{'storage-plugin'}->state_dir();
    make_path($state_dir);
    file_set_contents("$state_dir/$vmid.last-full", "$time\n");
}

# Proxmox VE can keep a bitmap of the parts of each disk that change after a backup (while the
# VM runs). The next backup then only gets to read these parts. The client can tell the server
# about the rest of a disk without reading it, as it keeps the checksums of the blocks of its
# last image backup (urbackup/hdat_img_<volume>.dat): the plugin hands it the changed parts,
# which it reads, and it compares the checksums of the others with those of the server.
#
# That needs the bitmaps to be of the changes since the image backup the client and the server
# last had of each disk, so the plugin only asks for them after a backup of the VM that the
# server has finished, and that this node has made of the VM process that still runs (the
# bitmaps move to other nodes with a VM, and may come back). The file with what that backup was
# of is the state for it. And the server has to compare with its last image backup, which is a
# setting of the virtual client there.
my sub bitmap_state_file {
    my ($self, $vmid) = @_;
    return $self->{'storage-plugin'}->state_dir() . "/$vmid.bitmap";
}

# Whether the server compares an incremental image backup of the guest with its last image
# backup. The client has the settings of the virtual client as the server last sent them; a new
# virtual client gets the ones of the plugin (see vm_client_settings()).
my sub incremental_to_last_image {
    my ($self, $vmid) = @_;

    my $fn = option($self, 'client-dir') . "/data/settings_" . virtual_client($vmid) . ".cfg";
    return 1 if !-e $fn;

    my $found = {};
    for my $line (split(/\n/, file_get_contents($fn))) {
        my ($key, $value) = $line =~ m/^((?:local|internet)_incr_image_style)=(.*?)\s*$/ or next;
        return 0 if $value ne 'to-last';
        $found->{$key} = 1;
    }
    return scalar(keys $found->%*) == 2;
}

# The process of a running VM: its ID and the time it was started at
my sub vm_process {
    my ($vmid) = @_;

    my $pid = eval {
        require PVE::QemuServer::Helpers;
        PVE::QemuServer::Helpers::vm_running_locally($vmid);
    } or return;
    my $stat = eval { file_get_contents("/proc/$pid/stat") } // return;
    # (the fields after the name, which can have spaces)
    my @fields = split(/ /, $stat =~ s/^.*\) //sr);
    return "$pid:$fields[19]";
}

my sub bitmap_state {
    my ($volumes) = @_;
    return { map { $_ => int($volumes->{$_}->{size}) } keys $volumes->%* };
}

# Whether the bitmaps of the VM, if it has them, are of the changes since its last backup.
# undef: no bitmaps for this VM at all.
my sub bitmaps_usable {
    my ($self, $vmid, $volumes) = @_;

    my $fn = bitmap_state_file($self, $vmid);
    return 0 if !-e $fn;
    my $state = eval { JSON->new->decode(file_get_contents($fn)) };
    return 0 if ref($state) ne 'HASH';
    return if $state->{unusable};
    return 0 if ref($state->{devices}) ne 'HASH';

    my $process = vm_process($vmid) // return 0;
    return 0 if ($state->{process} // '') ne $process;

    my $devices = bitmap_state($volumes);
    return JSON->new->canonical->encode($devices)
        eq JSON->new->canonical->encode($state->{devices});
}

# Blocks of the client's checksums: it reads all of a block that has a changed part
my $CHECKSUM_BLOCK_SIZE = 512 * 1024;

# Writes the list of the changed parts of a disk for the client: lines with offset and length in
# bytes.
#
# They are read with qemu-img, which shows a bitmap of an export in place of where the data of
# the image is: the parts without data are the changed ones.
my sub write_changed_extents {
    my ($fn, $nbd_path, $device_name, $bitmap_name, $size) = @_;

    my $image = "driver=nbd,server.type=unix,server.path=" . ($nbd_path =~ s/,/,,/gr)
        . ",export=$device_name,x-dirty-bitmap=qemu:dirty-bitmap:$bitmap_name";

    my $extents = '';
    my $changed = 0;
    my $next = 0;
    my $fits = 1;
    run_command(
        ['qemu-img', 'map', '--output=json', '--image-opts', $image],
        outfunc => sub {
            my ($line) = @_;
            return if $line !~ m/^[\[\s,]*\{/;
            my ($start) = $line =~ m/"start":\s*(\d+)\b/;
            my ($length) = $line =~ m/"length":\s*(\d+)\b/;
            my ($data) = $line =~ m/"data":\s*(true|false)\b/;
            die "unexpected output of qemu-img: $line\n"
                if !defined($start) || !$length || !defined($data) || $start != $next;
            $next = $start + $length;
            return if $data eq 'true';

            $fits = 0
                if $start % $CHECKSUM_BLOCK_SIZE
                || ($length % $CHECKSUM_BLOCK_SIZE && $next != $size);
            $extents .= "$start $length\n";
            $changed += $length;
        },
        errmsg => "unable to read the bitmap of $device_name",
    );
    # (what is not listed is not backed up again)
    die "the bitmap of $device_name is of $next bytes, the disk has $size\n" if $next != $size;
    return if !$fits;

    file_set_contents($fn, $extents);
    return $changed;
}

# The backup processes of the client, see "urbackupclientctl status"
my sub client_status {
    my ($self) = @_;

    my ($rc, $json, $err) = $self->{'storage-plugin'}->clientctl($self->{scfg}, ['status']);
    die "urbackupclientctl status failed (exit code $rc): $err\n" if $rc != 0;
    my $status = eval { decode_json($json) };
    die "unexpected status from the UrBackup client - $@" if $@;
    return $status;
}

# Asks the server (via the client) to start a backup of the virtual client: an image backup, or
# else a file backup. Returns the exit code of urbackupclientctl.
my sub request_backup {
    my ($self, $vc, $image, $full) = @_;

    my ($rc, $out, $err) = eval {
        $self->{'storage-plugin'}->clientctl(
            $self->{scfg},
            [
                'start', $full ? '--full' : '--incremental', $image ? '--image' : (),
                '--non-blocking', '--virtual-client', $vc,
            ],
        );
    };
    die "urbackupclientctl failed - $@" if $@;

    # 0: asked the server
    # 2: the client is busy with another backup
    # 3: no server is connected to the virtual client (yet, e.g. first backup of this guest)
    return $rc if $rc == 0 || $rc == 2 || $rc == 3;

    die "unable to start the backup of $vc (urbackupclientctl exit code $rc): $err\n";
}

# Starts the image backup of the virtual client and waits until all its volumes are backed up.
#
# "urbackupclientctl start" without --non-blocking is not used for this: it only follows the
# backup of one volume, and cannot tell a server that starts the backup late (e.g. because it is
# busy) from one that never got the request. So the plugin watches the processes of the client:
# the image backup of a volume is a process with the volume as its details.
my sub run_image_backup {
    my ($self, $vmid, $full, $volumes) = @_;

    my $deadline = time() + option($self, 'backup-timeout');
    my $vc = virtual_client($vmid);

    my $is_image = sub { ($_[0]->{action} // '') =~ m/^(?:FULLI|INCRI)$/ };
    my $name = sub { $_[0] =~ s!^.*/!!r };

    my $pending = { map { $_ => 1 } $volumes->@* };
    my $backup_volume = sub {
        my ($proc) = @_;
        my $volume = $proc->{details} // '';
        return $is_image->($proc) && $pending->{$volume} ? $volume : undef;
    };

    # Processes from before this backup (e.g. of an earlier, aborted one)
    my $status = client_status($self);
    my $old = {
        map { $_->{process_id} => 1 }
            $status->{running_processes}->@*, $status->{finished_processes}->@*
    };

    my $procs = {}; # process id => volume
    my $next_request = 0;
    my $last_rc = -1;
    my $next_progress = time() + 60;
    my $request_interval = 120;
    my $status_errors = 0;

    while ($pending->%*) {
        if (time() >= $deadline) {
            die "timeout waiting for the UrBackup server to back up "
                . join(', ', map { $name->($_) } sort keys $pending->%*) . " of $vc\n";
        }

        $status = eval { client_status($self) };
        if (my $err = $@) {
            # e.g. the client is being restarted
            $err =~ s/\s+$//;
            die "the UrBackup client does not answer - is it running? ($err)\n"
                if ++$status_errors > 30;
            sleep(2);
            next;
        }
        $status_errors = 0;

        my $running = {};
        for my $proc ($status->{running_processes}->@*) {
            my $id = $proc->{process_id};
            next if $old->{$id};
            my $volume = $procs->{$id} // $backup_volume->($proc) // next;

            if (!$procs->{$id}) {
                my $type = $proc->{action} eq 'FULLI' ? 'full' : 'incremental';
                log_info($self, "$type image backup of " . $name->($volume) . " started");
                $procs->{$id} = $volume;
            }
            $running->{$id} = $proc;
            $next_request = time() + $request_interval;
        }

        for my $proc ($status->{finished_processes}->@*) {
            my $id = $proc->{process_id};
            next if $old->{$id};
            # (a short backup may never have been seen running)
            my $volume = $procs->{$id} // $backup_volume->($proc) // next;

            $old->{$id} = 1;
            delete $procs->{$id};
            $next_request = time() + $request_interval;

            die "image backup of " . $name->($volume) . " of $vc failed - see the logs of the"
                . " UrBackup server\n" if !$proc->{success};

            log_info($self, "image backup of " . $name->($volume) . " finished");
            delete $pending->{$volume};
        }

        # The client drops a process without a result if the server stops talking to it
        for my $id (keys $procs->%*) {
            next if $running->{$id};
            die "image backup of " . $name->($procs->{$id}) . " of $vc was aborted (lost the"
                . " connection to the UrBackup server)\n";
        }

        last if !$pending->%*;

        # Ask (again) while no volume is being backed up for some time. The server ignores the
        # request while it does not know that the virtual client can do image backups, and a
        # backup that fails on the server before it gets to the client cannot be seen here. A
        # request for a backup that is already waiting on the server changes nothing.
        if (!$running->%* && time() >= $next_request) {
            my $rc = request_backup($self, $vc, 1, $full);
            if ($rc == 0) {
                log_info($self, "asked the UrBackup server to back up $vc");
            } elsif ($rc != $last_rc) {
                log_info($self, "the UrBackup client is busy with another backup - waiting")
                    if $rc == 2;
                log_info($self, "waiting for the UrBackup server to connect to virtual client $vc")
                    if $rc == 3;
            }
            $last_rc = $rc;
            $next_request = time() + ($rc == 0 ? $request_interval : 30);
        }

        if (time() >= $next_progress) {
            for my $id (sort { $a <=> $b } keys $running->%*) {
                my $pc = $running->{$id}->{percent_done} // -1;
                log_info($self, $name->($procs->{$id}) . ": " . ($pc < 0 ? 'preparing' : "$pc%"));
            }
            if (!$running->%*) {
                log_info($self, "waiting for the UrBackup server to back up "
                    . join(', ', map { $name->($_) } sort keys $pending->%*));
            }
            $next_progress = time() + 60;
        }

        sleep(1);
    }
}

# Waits until the server has the images of all disks from this backup job.
#
# The client is done with the image backup of a disk when it has sent the data. The server still
# has to finish the image then, does not tell the client about it, and may fail (or be gone). It
# only lists images it has finished.
my sub wait_for_server_images {
    my ($self, $vmid) = @_;

    my $vc = virtual_client($vmid);
    my $backup_time = $self->{$vmid}->{'backup-time'};
    my $deadline = time() + $SERVER_FINISH_TIMEOUT;
    my $logged = 0;

    while (1) {
        my $backups = eval {
            $self->{'storage-plugin'}->vm_backups($self->{scfg}, $self->{storeid}, $vmid);
        };
        my $err = $@;
        return if !$err && grep { $_->{time} == $backup_time } $backups->@*;

        if (time() >= $deadline) {
            $err ||= "it does not have finished images of all disks from this backup\n";
            die "the UrBackup server did not finish the image backup of $vc - $err";
        }

        log_info($self, "waiting for the UrBackup server to finish the image backup of $vc")
            if !$logged++;
        sleep(5);
    }
}

# Asks the server for a file backup of the virtual client of a container and waits until the
# server has it. Returns the size the server tells for it.
#
# The client does not tell which of its virtual clients a file backup is of. The file backups
# that start after the request are taken as the one of the container: vzdump backs up one guest
# after the other, and the client does one file backup at a time.
my sub run_file_backup {
    my ($self, $vmid) = @_;

    my $backup_time = $self->{$vmid}->{'backup-time'};
    my $deadline = time() + option($self, 'backup-timeout');
    my $vc = virtual_client($vmid);

    my $is_file_backup = sub { ($_[0]->{action} // '') =~ m/^(?:R_)?(?:FULL|INCR)$/ };

    my $status = client_status($self);
    my $old = {
        map { $_->{process_id} => 1 }
            $status->{running_processes}->@*, $status->{finished_processes}->@*
    };

    my $procs = {};
    my $has_dirs = 0;
    my $dirs_deadline = time() + $SERVER_SETTINGS_TIMEOUT;
    my $requested = 0;
    # After a file backup, the server still talks to the client about it for a moment, for which
    # the client shows a process again that then ends without a result
    my $finished = 0;
    my $next_request = 0;
    my $last_rc = -1;
    my $next_check = 0;
    my $next_progress = time() + 60;
    my $status_errors = 0;
    # The server does a second file backup if it is asked again while the first one waits, so
    # only ask again after a long time without a backup (it may have failed on the server
    # before it got to the client)
    my $request_interval = 600;

    while (1) {
        die "timeout waiting for the UrBackup server to back up $vc\n" if time() >= $deadline;

        $status = eval { client_status($self) };
        if (my $err = $@) {
            # e.g. the client is being restarted
            $err =~ s/\s+$//;
            die "the UrBackup client does not answer - is it running? ($err)\n"
                if ++$status_errors > 30;
            sleep(2);
            next;
        }
        $status_errors = 0;

        my $running = {};
        for my $proc ($status->{running_processes}->@*) {
            my $id = $proc->{process_id};
            next if $old->{$id};
            next if !$procs->{$id} && !($requested && $is_file_backup->($proc));

            if (!$procs->{$id}) {
                if ($finished) {
                    $old->{$id} = 1;
                    next;
                }
                my $type = $proc->{action} =~ m/FULL/ ? 'full' : 'incremental';
                log_info($self, "$type file backup started");
                $procs->{$id} = 1;
            }
            $running->{$id} = $proc;
            $next_request = time() + $request_interval;
        }

        for my $proc ($status->{finished_processes}->@*) {
            my $id = $proc->{process_id};
            next if $old->{$id};
            # (a short backup may never have been seen running)
            next if !$procs->{$id} && !($requested && $is_file_backup->($proc));

            $old->{$id} = 1;
            delete $procs->{$id};
            $next_request = time() + $request_interval;

            die "file backup of $vc failed - see the logs of the UrBackup server\n"
                if !$proc->{success};

            log_info($self, "file backup finished");
            $finished = 1;
            $next_check = 0;
        }

        # The client drops a process without a result if the server stops talking to it, which
        # the server also does if the backup fails early (e.g. the client cannot read a
        # directory)
        for my $id (keys $procs->%*) {
            next if $running->{$id};
            die "file backup of $vc did not finish - see the logs of the UrBackup server\n";
        }

        # The server has the backup when it lists it, which is a bit after the client is done
        if ($requested && !$running->%* && time() >= $next_check) {
            my $backups = eval {
                $self->{'storage-plugin'}->container_backups($self->{scfg}, $vmid);
            };
            for my $backup (($backups // [])->@*) {
                return $backup->{size} if $backup->{time} == $backup_time;
            }
            $next_check = time() + 5;
        }

        if (!$has_dirs && !($has_dirs = client_has_container_dirs($self, $vmid))) {
            die "the UrBackup server did not give the directories of the container to virtual"
                . " client $vc - check the directories to back up of client '"
                . $self->{'storage-plugin'}->server_client_name($self->{scfg}, $vmid)
                . "' on the server\n" if time() >= $dirs_deadline;
            log_info($self, "waiting for the UrBackup server to set up virtual client $vc")
                if $last_rc == -1;
            $last_rc = -2;
            sleep(5);
            next;
        }

        if (!$running->%* && time() >= $next_request) {
            my $rc = request_backup($self, $vc, 0, 0);
            if ($rc == 0) {
                log_info($self, "asked the UrBackup server to back up $vc");
                $requested = 1;
                $finished = 0;
            } elsif ($rc != $last_rc) {
                log_info($self, "the UrBackup client is busy with another backup - waiting")
                    if $rc == 2;
                log_info($self, "waiting for the UrBackup server to connect to virtual client $vc")
                    if $rc == 3;
            }
            $last_rc = $rc;
            $next_request = time() + ($rc == 0 ? $request_interval : 30);
        }

        if (time() >= $next_progress) {
            for my $id (sort { $a <=> $b } keys $running->%*) {
                my $pc = $running->{$id}->{percent_done} // -1;
                log_info($self, "file backup: " . ($pc < 0 ? 'preparing' : "$pc%"));
            }
            log_info($self, "waiting for the UrBackup server to back up $vc") if !$running->%*;
            $next_progress = time() + 60;
        }

        sleep(1);
    }
}

# The configuration of a container as vzdump stores it in its backups (see assemble in
# PVE::VZDump::LXC)
my sub container_config {
    my ($vmid) = @_;

    require PVE::LXC::Config;

    my $conf = PVE::LXC::Config->load_config($vmid);
    delete $conf->@{qw(lock snapshots parent pending)};

    return PVE::LXC::Config::write_pct_config("/lxc/$vmid.conf", $conf);
}

# The user and group IDs of a container on the host as an option of mount(8) that shows a file
# system with the IDs of the container. Empty for a privileged container.
my sub container_idmap {
    my ($vmid) = @_;

    require PVE::LXC;
    require PVE::LXC::Config;

    my ($id_map) = PVE::LXC::parse_id_maps(PVE::LXC::Config->load_config($vmid));

    return join(' ', map {
        my ($type, $container_id, $host_id, $count) = $_->@*;
        "$type:$host_id:$container_id:$count"
    } $id_map->@*);
}

# Backup Provider API

sub new {
    my ($class, $storage_plugin, $scfg, $storeid, $log_function) = @_;

    my $self = bless {
        scfg => $scfg,
        storeid => $storeid,
        'storage-plugin' => $storage_plugin,
        'log-function' => $log_function,
    }, $class;

    return $self;
}

sub provider_name {
    my ($self) = @_;
    return 'UrBackup';
}

sub job_init {
    my ($self, $start_time) = @_;

    if (!-e '/sys/module/nbd/parameters/nbds_max') {
        die "required 'nbd' kernel module not loaded - load it with 'modprobe nbd nbds_max=128'\n";
    }

    for my $script (qw(create_volume_snapshot remove_volume_snapshot)) {
        die "snapshot script $SNAPSHOT_SCRIPT_DIR/$script is missing\n"
            if !-x "$SNAPSHOT_SCRIPT_DIR/$script";
    }

    return;
}

sub job_cleanup {
    my ($self) = @_;
    return;
}

sub backup_init {
    my ($self, $vmid, $vmtype, $backup_time) = @_;

    $vmid = guest_id($vmid);
    ($backup_time) = $backup_time =~ m/^(\d+)$/ or die "unexpected backup time\n";

    my $archive = "${vmid}/${vmtype}-${backup_time}";
    $self->{$vmid} = {
        'backup-time' => $backup_time,
        archive => $archive,
        'nbd-nodes' => [],
    };

    return { 'archive-name' => $archive };
}

sub backup_cleanup {
    my ($self, $vmid, $vmtype, $success, $info) = @_;

    $vmid = guest_id($vmid);

    cleanup_backup($self, $vmid);

    if ($success) {
        # (of a VM, the size of the disks: the server does not tell how much space the images
        # take)
        return { stats => { 'archive-size' => $self->{$vmid}->{size} // 0 } };
    }

    return;
}

sub backup_get_mechanism {
    my ($self, $vmid, $vmtype) = @_;

    return $vmtype eq 'qemu' ? 'nbd' : 'directory';
}

sub backup_handle_log_file {
    my ($self, $vmid, $filename) = @_;
    return;
}

sub backup_vm_query_incremental {
    my ($self, $vmid, $volumes) = @_;

    $vmid = guest_id($vmid);

    # A full image backup reads all of the disks, as does a backup that cannot be sure what the
    # bitmaps are of. Both start new ones.
    my $full = full_image_due($self, $vmid);
    $self->{$vmid}->{full} = $full;

    my $usable = bitmaps_usable($self, $vmid, $volumes);
    return if !defined($usable);

    if (!incremental_to_last_image($self, $vmid)) {
        log_info($self, "reading all of the disks: for only reading what has changed, set the"
            . " incremental image style of " . virtual_client($vmid) . " on the UrBackup server"
            . " to 'based on last image backup'");
        return;
    }

    my $mode = $usable && !$full ? 'use' : 'new';
    return { map { $_ => $mode } keys $volumes->%* };
}

sub backup_vm {
    my ($self, $vmid, $guest_config, $volumes, $info) = @_;

    $vmid = guest_id($vmid);

    eval {
        remove_run_dir($self, $vmid);
        make_path("$RUN_DIR/$vmid");

        # Until this backup is done, there is none that the bitmaps of the next one can be since
        my $state_fn = bitmap_state_file($self, $vmid);
        my $no_bitmaps = !defined(bitmaps_usable($self, $vmid, $volumes));
        unlink($state_fn) if !$no_bitmaps;

        my @backup_volumes;
        my $devices = [sort keys $volumes->%*];
        for my $device_name ($devices->@*) {
            # (it is part of file names)
            die "unexpected device name '$device_name'\n" if $device_name !~ m/^[\w.-]+$/;

            my $nbd_path = $volumes->{$device_name}->{'nbd-path'}
                or die "no NBD export for $device_name\n";

            my $node = bind_next_free_dev_nbd_node(
                $self, ["nbd:unix:${nbd_path}:exportname=${device_name}", "--format=raw", "--read-only"]);
            push $self->{$vmid}->{'nbd-nodes'}->@*, $node;

            my $link = "$RUN_DIR/$vmid/$device_name";
            symlink($node, $link) or die "unable to create $link - $!\n";
            push @backup_volumes, $link;
            $self->{$vmid}->{size} += $volumes->{$device_name}->{size};

            write_disk_info(
                $self, $vmid, $device_name, $volumes->{$device_name}->{size}, $devices,
                $guest_config, $info->{'firewall-config'},
            );
            log_info($self, "$device_name: via $node");

            # What the snapshot script tells the client about the changes of the disk: the list
            # of them, or that all of it may have changed. Without a bitmap the client compares
            # all of the disk with what the server has, without keeping checksums.
            my $bitmap_mode = $volumes->{$device_name}->{'bitmap-mode'} // 'none';
            if ($bitmap_mode eq 'reuse') {
                my ($bitmap_name) = ($volumes->{$device_name}->{'bitmap-name'} // '')
                    =~ m/^([\w.:-]+)$/ or die "unexpected name of the bitmap of $device_name\n";
                my $changed = write_changed_extents(
                    "$link.changed", $nbd_path, $device_name, $bitmap_name,
                    $volumes->{$device_name}->{size},
                );
                if (!defined($changed)) {
                    # (only the changed parts can be read now, so not in this backup)
                    make_path($self->{'storage-plugin'}->state_dir());
                    file_set_contents($state_fn, JSON->new->encode({ unusable => 1 }));
                    die "the bitmap of $device_name does not fit the blocks of the UrBackup"
                        . " client - the next backups of the VM are without bitmaps\n";
                }
            } elsif ($bitmap_mode eq 'new') {
                file_set_contents("$link.all-changed", '');
            }
        }

        configure_client($self, $vmid, vm_client_settings(\@backup_volumes), 1);

        my $full = $self->{$vmid}->{full} // full_image_due($self, $vmid);
        my $start_time = time();
        log_info($self, "starting " . ($full ? 'full' : 'incremental') . " image backup of"
            . " virtual client " . virtual_client($vmid));
        run_image_backup($self, $vmid, $full, \@backup_volumes);
        wait_for_server_images($self, $vmid);
        record_full_image($self, $vmid, $start_time) if $full;

        if (!$no_bitmaps && defined(my $process = vm_process($vmid))) {
            make_path($self->{'storage-plugin'}->state_dir());
            file_set_contents($state_fn, JSON->new->canonical->encode({
                devices => bitmap_state($volumes),
                process => $process,
            }));
        }
    };
    my $err = $@;

    cleanup_backup($self, $vmid);

    die $err if $err;

    return;
}

# Backs up the container.
#
# This is not left to backup_container(), which runs as the root user of the container: for an
# unprivileged container that is a user of the host who may not talk to the client. All it
# would add are the configuration of the container as vzdump stores it, which is made here in
# the same way, and the files that the job excludes (see there).
sub backup_container_prepare {
    my ($self, $vmid, $info) = @_;

    $vmid = guest_id($vmid);

    my $plugin = $self->{'storage-plugin'};
    my $backup_time = $self->{$vmid}->{'backup-time'};
    my ($directory) = ($info->{directory} // '') =~ m!^(/[^\0\n]*?)/*$!
        or die "unexpected directory of the container\n";

    remove_run_dir($self, $vmid);
    die "unable to remove $RUN_DIR/$vmid of an earlier backup\n" if -e "$RUN_DIR/$vmid";

    eval {
        # Only root may get to the file system: with the IDs of the container, its files belong to
        # other users here
        my $rootfs = $plugin->container_rootfs_dir($vmid);
        my $config_dir = $plugin->container_config_dir($vmid);
        make_path($rootfs, $config_dir);
        for my $dir ($rootfs =~ s!/[^/]+$!!r, $config_dir) {
            chmod(0700, $dir) or die "unable to protect $dir - $!\n";
        }

        my $idmap = container_idmap($vmid);
        my $options = 'ro,nosuid,nodev' . ($idmap ne '' ? ",X-mount.idmap=$idmap" : '');
        # "." or "./", and the mount points in it after their parents
        my @sources = sort map {
            my ($path) = $_ =~ m!^\.(?:/+([^\0\n]*?))?/*$! or die "unexpected source '$_'\n";
            $path //= '';
            die "unexpected source '$_'\n" if $path =~ m!(?:^|/)\.\.(?:/|$)!;
            $path;
        } $info->{sources}->@*;
        for my $path (@sources) {
            my $suffix = $path eq '' ? '' : "/$path";
            run_command(
                ['mount', '--bind', '-o', $options, "$directory$suffix", "$rootfs$suffix"],
                errmsg => "unable to mount the file system of the container at $rootfs$suffix",
            );
        }
        move_mounts_to_host($rootfs);

        my $guest_config = container_config($vmid);
        # (the file names in statements of their own: vzdump runs with -T, and the contents taint
        # what else a statement makes)
        my $config_fn = "$config_dir/pct.conf";
        my $firewall_fn = "$config_dir/firewall.fw";
        # The name of this file tells the storage plugin which backup job a file backup is of
        my $job_fn = "$config_dir/job-$backup_time";
        file_set_contents($config_fn, $guest_config);
        file_set_contents($firewall_fn, $info->{'firewall-config'})
            if defined($info->{'firewall-config'});
        file_set_contents($job_fn, "$backup_time\n");

        configure_client($self, $vmid, container_client_settings($self, $vmid), 0);

        log_info($self, "starting file backup of virtual client " . virtual_client($vmid));
        $self->{$vmid}->{size} = run_file_backup($self, $vmid);
        $self->{$vmid}->{config} = $guest_config;
    };
    my $err = $@;

    # Now, and not in backup_cleanup(): vzdump removes its snapshot of the container before it
    # calls that after an error, which it cannot while the snapshot is mounted
    remove_run_dir($self, $vmid);

    die $err if $err;

    return;
}

sub backup_container {
    my ($self, $vmid, $guest_config, $exclude_patterns, $info) = @_;

    log_warning($self, "the configuration of the container in the backup is not the one of vzdump")
        if ($self->{$vmid}->{config} // '') ne $guest_config;

    # The server takes the excluded files from the plugin once, when it gets to know the virtual
    # client. After that they are a setting of the client on the server.
    my $excluded = { map { $_ => 1 } $CONTAINER_EXCLUDES->@* };
    if (my @other = grep { !$excluded->{$_} } ($exclude_patterns // [])->@*) {
        log_info($self, "the files to exclude are a setting on the UrBackup server (of client '"
            . $self->{'storage-plugin'}->server_client_name($self->{scfg}, $vmid)
            . "') - not used from this job: " . join(', ', @other));
    }

    return;
}

# The backup of a volume name "backup/<vmid>/<qemu|lxc>-<time>": ($vmid, $backup), see
# vm_backups and container_backups of the storage plugin
my sub find_backup {
    my ($self, $volname) = @_;

    my $plugin = $self->{'storage-plugin'};
    my (undef, $name, $vmid) = $plugin->parse_volname($volname);
    my ($type, $time) = $name =~ m!/(\w+)-(\d+)$!;

    my $backups = $type eq 'lxc'
        ? $plugin->container_backups($self->{scfg}, $vmid)
        : $plugin->vm_backups($self->{scfg}, $self->{storeid}, $vmid);
    for my $backup ($backups->@*) {
        return ($vmid, $backup) if $backup->{time} == $time;
    }

    die "backup '$volname' not found on the UrBackup server\n";
}

my sub is_container_backup {
    my ($self, $volname) = @_;

    my (undef, $name) = $self->{'storage-plugin'}->parse_volname($volname);
    return $name =~ m!/lxc-!;
}

# Has the client restore a directory of a file backup of the guest's virtual client to another
# directory. $name, $source: the name of the directory in the backup, and where it was during
# the backup.
my sub restore_directory {
    my ($self, $vmid, $backup, $name, $source, $target, $log_progress) = @_;

    my ($rc, $out, $err) = $self->{'storage-plugin'}->clientctl(
        $self->{scfg},
        [
            'restore-start', '--non-blocking', '--virtual-client', virtual_client($vmid),
            '--backupid', $backup->{id}, '--path', $name,
            '--map-from', $source, '--map-to', $target,
        ],
    );
    my $restore = $rc == 0 ? eval { decode_json($out) } : undef;
    if (ref($restore) ne 'HASH' || !$restore->{ok}) {
        $err = "error code $restore->{err} from the UrBackup server"
            if ref($restore) eq 'HASH' && defined($restore->{err});
        die "unable to start the restore of '$name' by the UrBackup client"
            . " (urbackupclientctl exit code $rc): $err\n";
    }
    my $id = $restore->{process_id} // die "the UrBackup client did not start a restore\n";

    my $seen = 0;
    my $appear_deadline = time() + 120;
    my $next_progress = time() + 60;
    my $status_errors = 0;
    while (1) {
        my $status = eval { client_status($self) };
        if (my $status_err = $@) {
            $status_err =~ s/\s+$//;
            die "the UrBackup client does not answer - is it running? ($status_err)\n"
                if ++$status_errors > 30;
            sleep(2);
            next;
        }
        $status_errors = 0;

        for my $proc ($status->{finished_processes}->@*) {
            next if $proc->{process_id} != $id;
            return if $proc->{success};
            die "restoring '$name' from the UrBackup server failed - see the log of the UrBackup"
                . " client\n";
        }

        my ($proc) = grep { $_->{process_id} == $id } $status->{running_processes}->@*;
        if ($proc) {
            $seen = 1;
            if ($log_progress && time() >= $next_progress) {
                my $pc = $proc->{percent_done} // -1;
                log_info($self, "restoring: " . ($pc < 0 ? 'preparing' : "$pc%"));
                $next_progress = time() + 60;
            }
        } elsif ($seen || time() >= $appear_deadline) {
            die "restoring '$name' from the UrBackup server was aborted\n";
        }

        sleep(1);
    }
}

# The configuration files of a container backup: { name => content }
my sub container_config_files {
    my ($self, $volname) = @_;

    return $self->{'restore-files'}->{$volname} if $self->{'restore-files'}->{$volname};

    my ($vmid, $backup) = find_backup($self, $volname);

    make_path($RUN_DIR);
    my $tmp = File::Temp->newdir('restore-config-XXXXXX', DIR => $RUN_DIR);
    my $plugin = $self->{'storage-plugin'};
    restore_directory(
        $self, $vmid, $backup, $plugin->container_config_name($vmid),
        $plugin->container_config_dir($vmid), $tmp->dirname,
    );

    my $files = {};
    for my $name (qw(pct.conf firewall.fw)) {
        my $fn = $tmp->dirname . "/$name";
        $files->{$name} = file_get_contents($fn) if -e $fn;
    }

    $self->{'restore-files'}->{$volname} = $files;
    return $files;
}

# The directory the file system of a container is restored to, below the one of the storage
my sub restore_container_dir {
    my ($self, $volname) = @_;

    my (undef, $name, $vmid) = $self->{'storage-plugin'}->parse_volname($volname);
    my ($time) = $name =~ m/-(\d+)$/;

    return option($self, 'restore-dir') . "/urbackup-restore-$vmid-$time-lxc";
}

# The disks of a backup: { $device_name => { image, size, files } }
my sub backup_disks {
    my ($self, $volname) = @_;

    return $self->{'restore-disks'}->{$volname} if $self->{'restore-disks'}->{$volname};

    my ($vmid, $backup) = find_backup($self, $volname);

    my $disks = {};
    for my $image ($backup->{images}->@*) {
        my $files =
            $self->{'storage-plugin'}->image_files($self->{scfg}, $self->{storeid}, $image)
            // die "image '$image->{volume}' is not an image of a whole disk - cannot restore it\n";
        my $disk = eval { decode_json($files->{'disk.json'} // '') };
        die "unexpected disk information of image '$image->{volume}'\n"
            if !$disk || ($disk->{device} // '') !~ m/^([\w.-]+)$/;
        my $device_name = $1;
        my ($size) = ($disk->{size} // '') =~ m/^(\d+)$/
            or die "no size in the disk information of image '$image->{volume}'\n";

        $disks->{$device_name} = { image => $image, size => int($size), files => $files };
    }
    die "backup '$volname' has no disks\n" if !$disks->%*;

    $self->{'restore-disks'}->{$volname} = $disks;
    return $disks;
}

# Removes the files and directories that restores left in the restore directory long ago
my sub remove_restore_leftovers {
    my ($self) = @_;

    my $dir = option($self, 'restore-dir');
    opendir(my $dh, $dir) or return;
    for my $entry (readdir($dh)) {
        my ($name) = $entry =~ m/^(urbackup-restore-\d+-\d+-[\w.-]+)$/ or next;
        my $path = "$dir/$name";
        my $st = File::stat::lstat($path) or next;
        next if time() - $st->mtime < $RESTORE_LEFTOVER_DAYS * 24 * 3600;

        log_info($self, "removing $path of an earlier restore");
        if (-d $st) {
            remove_tree($path);
        } else {
            unlink($path);
        }
    }
    closedir($dh);
}

# The file a disk is downloaded to for the restore
my sub restore_image_file {
    my ($self, $volname, $device_name) = @_;

    my (undef, $name, $vmid) = $self->{'storage-plugin'}->parse_volname($volname);
    my ($time) = $name =~ m/-(\d+)$/;
    ($device_name) = $device_name =~ m/^([\w.-]+)$/ or die "unexpected device name\n";

    return option($self, 'restore-dir') . "/urbackup-restore-$vmid-$time-$device_name.raw";
}

sub restore_get_mechanism {
    my ($self, $volname) = @_;

    return is_container_backup($self, $volname) ? ('directory', 'lxc') : ('qemu-img', 'qemu');
}

sub archive_get_guest_config {
    my ($self, $volname) = @_;

    if (is_container_backup($self, $volname)) {
        return container_config_files($self, $volname)->{'pct.conf'}
            // die "backup '$volname' has no guest configuration\n";
    }

    my $disks = backup_disks($self, $volname);
    my ($first) = sort keys $disks->%*;

    return $disks->{$first}->{files}->{'qemu-server.conf'}
        // die "backup '$volname' has no guest configuration\n";
}

sub archive_get_firewall_config {
    my ($self, $volname) = @_;

    return container_config_files($self, $volname)->{'firewall.fw'}
        if is_container_backup($self, $volname);

    my $disks = backup_disks($self, $volname);
    my ($first) = sort keys $disks->%*;

    return $disks->{$first}->{files}->{'firewall.fw'};
}

sub restore_vm_init {
    my ($self, $volname) = @_;

    remove_restore_leftovers($self);

    my $disks = backup_disks($self, $volname);

    return { map { $_ => { size => $disks->{$_}->{size} } } keys $disks->%* };
}

sub restore_vm_cleanup {
    my ($self, $volname) = @_;

    delete $self->{'restore-disks'}->{$volname};

    return;
}

sub restore_vm_volume_init {
    my ($self, $volname, $device_name, $info) = @_;

    my $disk = backup_disks($self, $volname)->{$device_name}
        or die "backup '$volname' has no disk '$device_name'\n";
    my $image = $disk->{image};

    make_path(option($self, 'restore-dir'));
    my $fn = restore_image_file($self, $volname, $device_name);

    # The client only writes the parts of the image that the server sends (not always the whole
    # disk), into a file that has to be large enough. The server's image is somewhat larger
    # than the disk.
    my $fh = IO::File->new($fn, 'w', 0600) or die "unable to create $fn - $!\n";
    truncate($fh, $disk->{size} + 1024 * 1024) or die "unable to resize $fn - $!\n";
    close($fh);

    log_info($self, "downloading the image of $device_name from the UrBackup server to $fn");
    my ($rc, $out, $err) = $self->{'storage-plugin'}->client_download(
        $self->{scfg},
        $self->{storeid},
        'download_image',
        {
            restore_img_id => $image->{id},
            restore_time => $image->{time},
            restore_out => $fn,
        },
    );
    if ($rc != 0) {
        unlink($fn);
        die "unable to download the image of $device_name from the UrBackup server"
            . " (urbackupclientbackend exit code $rc): $err\n";
    }

    truncate($fn, $disk->{size}) or die "unable to resize $fn - $!\n";

    return { 'qemu-img-path' => $fn };
}

sub restore_vm_volume_cleanup {
    my ($self, $volname, $device_name, $info) = @_;

    my $fn = restore_image_file($self, $volname, $device_name);
    unlink($fn) if -e $fn;

    return;
}

# (pve-container also passes the storage after $volname)
sub restore_container_init {
    my ($self, $volname) = @_;

    my ($vmid, $backup) = find_backup($self, $volname);

    remove_restore_leftovers($self);

    # Only root may get to the files: they have the IDs of the container
    my $dir = restore_container_dir($self, $volname);
    remove_tree($dir) if -e $dir;
    make_path($dir, { mode => 0700 });
    mkdir("$dir/rootfs") or die "unable to create $dir/rootfs - $!\n";

    log_info($self, "restoring the files of the container from the UrBackup server to $dir/rootfs");
    my $plugin = $self->{'storage-plugin'};
    restore_directory(
        $self, $vmid, $backup, $plugin->container_rootfs_name($vmid),
        $plugin->container_rootfs_dir($vmid), "$dir/rootfs", 1,
    );

    return { 'archive-directory' => "$dir/rootfs" };
}

sub restore_container_cleanup {
    my ($self, $volname) = @_;

    delete $self->{'restore-files'}->{$volname};

    my $dir = restore_container_dir($self, $volname);
    remove_tree($dir) if -e $dir;

    return;
}

1;
