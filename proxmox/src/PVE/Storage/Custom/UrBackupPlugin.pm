package PVE::Storage::Custom::UrBackupPlugin;

# Proxmox VE storage plugin for backups with UrBackup.
#
# The backups are made by the UrBackup client running on the Proxmox VE host: each guest is a
# virtual client "pve-<vmid>" of it. The disks of a VM are image backups of that virtual client,
# the file system of a container is a file backup of it.
# See PVE::BackupProvider::Plugin::UrBackup for how a backup is made.

use strict;
use warnings;

use File::Path qw(make_path);
use File::Temp ();
use IO::Uncompress::Unzip qw($UnzipError);
use JSON;

use PVE::INotify;
use PVE::Storage;
use PVE::Tools qw(file_get_contents file_read_firstline file_set_contents run_command);

use PVE::BackupProvider::Plugin::UrBackup;

use base qw(PVE::Storage::Plugin);

my $RUN_DIR = '/run/urbackup-pve';

# Configuration

sub api {
    # Backup providers exist since API version 11
    my $supported_apiver_min = 11;
    my $supported_apiver_max = 15;

    my $api_ver = PVE::Storage::APIVER;
    if ($api_ver >= $supported_apiver_min && $api_ver <= $supported_apiver_max) {
        return $api_ver;
    }

    return $supported_apiver_max;
}

sub type {
    return 'urbackup';
}

sub plugindata {
    return {
        content => [{ backup => 1, none => 1 }, { backup => 1 }],
        features => { 'backup-provider' => 1 },
        'sensitive-properties' => { password => 1 },
    };
}

sub properties {
    return {
        'client-dir' => {
            description => "Data directory of the UrBackup client (contains 'data').",
            type => 'string',
            default => '/usr/local/var/urbackup',
        },
        'client-conf-dir' => {
            description => "Configuration directory of the UrBackup client (contains 'snapshot.cfg').",
            type => 'string',
            default => '/usr/local/etc/urbackup',
        },
        'clientctl' => {
            description => "Path of urbackupclientctl.",
            type => 'string',
            default => '/usr/local/bin/urbackupclientctl',
        },
        'client-backend' => {
            description => "Path of urbackupclientbackend.",
            type => 'string',
            default => '/usr/local/sbin/urbackupclientbackend',
        },
        'client-name' => {
            description => "Name of the host's UrBackup client on the server (default: the computer"
                . " name in the client's settings, else the host name).",
            type => 'string',
        },
        'restore-dir' => {
            description => "Directory for the disk images of a guest while it is restored (needs"
                . " space for up to the size of the disks).",
            type => 'string',
            default => '/var/tmp',
        },
        'backup-timeout' => {
            description => "Seconds to wait for the UrBackup server to start and finish the backup of"
                . " a guest (including the time it waits in the server's queue).",
            type => 'integer',
            minimum => 60,
            default => 4 * 3600,
        },
        'full-image-interval' => {
            description => "Days after which the next backup of a guest is a full image backup"
                . " (0: only the first one).",
            type => 'integer',
            minimum => 0,
            default => 30,
        },
    };
}

sub options {
    return {
        'client-dir' => { optional => 1 },
        'client-conf-dir' => { optional => 1 },
        'clientctl' => { optional => 1 },
        'client-backend' => { optional => 1 },
        'client-name' => { optional => 1 },
        'restore-dir' => { optional => 1 },
        'backup-timeout' => { optional => 1 },
        'full-image-interval' => { optional => 1 },
        # User of the UrBackup server for listing and restoring backups (if the server has users)
        username => { optional => 1 },
        password => { optional => 1 },
        disable => { optional => 1 },
        nodes => { optional => 1 },
    };
}

sub get_option {
    my ($class, $scfg, $name) = @_;
    my $value = $scfg->{$name} // properties()->{$name}->{default};
    # Untaint: vzdump runs with -T and the values are paths, names and numbers from storage.cfg
    ($value) = $value =~ m/^([^\0\n]*)$/ if defined($value);
    return $value;
}

# State of the plugin on this node, e.g. the time of the last full image backup of a guest
sub state_dir {
    return '/var/lib/urbackup-pve';
}

# The password of the server's user is kept outside of storage.cfg, like the passwords of other
# storage types
my sub password_file {
    my ($storeid) = @_;
    return "/etc/pve/priv/storage/${storeid}.pw";
}

my sub set_password {
    my ($storeid, $password) = @_;

    if (defined($password)) {
        mkdir "/etc/pve/priv/storage";
        file_set_contents(password_file($storeid), "$password\n", 0600, 1);
    } else {
        unlink(password_file($storeid));
    }
}

sub on_add_hook {
    my ($class, $storeid, $scfg, %sensitive) = @_;
    set_password($storeid, $sensitive{password});
    return;
}

sub on_update_hook {
    my ($class, $storeid, $scfg, %sensitive) = @_;
    set_password($storeid, $sensitive{password}) if exists($sensitive{password});
    return;
}

sub on_delete_hook {
    my ($class, $storeid, $scfg) = @_;
    set_password($storeid, undef);
    return;
}

# Where the backup provider makes the file system of a container available to the client during
# a backup, and its configuration. The server keeps these paths with the backups: a restore
# names them to restore to other directories.
sub container_rootfs_dir {
    my ($class, $vmid) = @_;
    return "$RUN_DIR/$vmid/fs/rootfs";
}

sub container_config_dir {
    my ($class, $vmid) = @_;
    return "$RUN_DIR/$vmid/config";
}

# The names of these directories in the file backups. The directories of all virtual clients of
# a client need different names (the client changes a name that it already has).
sub container_rootfs_name {
    my ($class, $vmid) = @_;
    return "pve-$vmid-rootfs";
}

sub container_config_name {
    my ($class, $vmid) = @_;
    return "pve-$vmid-config";
}

# The UrBackup server's name for the virtual client of a guest
sub server_client_name {
    my ($class, $scfg, $vmid) = @_;

    my $name = $class->get_option($scfg, 'client-name');
    if (!defined($name)) {
        my $settings_fn = $class->get_option($scfg, 'client-dir') . "/data/settings.cfg";
        if (-e $settings_fn && file_get_contents($settings_fn) =~ m/^computername=(\S.*?)\s*$/m) {
            $name = $1;
        }
    }
    $name //= PVE::INotify::nodename();

    return "${name}[pve-${vmid}]";
}

# What the exit codes of the client's restore commands mean (it logs the reason to its log file)
my $LIST_ERRORS = {
    1 => 'cannot connect to the UrBackup client - is it running?',
    2 => 'the UrBackup client did not answer',
    3 => 'the UrBackup client is not connected to a server',
};
my $DOWNLOAD_ERRORS = {
    2 => 'cannot open the output file',
    3 => 'the UrBackup server did not send the image',
    4 => 'lost the connection to the UrBackup server',
    5 => 'lost the connection to the UrBackup server',
    6 => 'writing to the output file failed',
    10 => 'cannot connect to the UrBackup client - is it running?',
    11 => 'the output file is too small for the image',
};

# Runs a restore command of the client (restore_cmd in urbackupclient/client_restore.cpp) with
# the given parameters. Returns its exit code and output.
sub client_restore_cmd {
    my ($class, $scfg, $cmd, $params, $timeout) = @_;

    # The client runs with the parent of its data directory as working directory
    my $work_dir = $class->get_option($scfg, 'client-dir') =~ s!/+[^/]+/*$!!r;
    my ($out, $err) = ('', '');
    my $rc = run_command(
        [
            'env', '-C', $work_dir, $class->get_option($scfg, 'client-backend'),
            '--internal', '--no-server', '--restore', 'true', '--restore_cmd', $cmd,
            map { ("--$_", $params->{$_}) } sort keys $params->%*,
        ],
        outfunc => sub { $out .= "$_[0]\n" },
        errfunc => sub { $err .= "$_[0]\n" },
        timeout => $timeout,
        noerr => 1,
    );
    $err =~ s/\s+$//;
    if ($rc != 0 && $err eq '') {
        my $errors = $cmd =~ m/^download_/ ? $DOWNLOAD_ERRORS : $LIST_ERRORS;
        $err = $errors->{$rc} // '';
    }

    return ($rc, $out, $err);
}

# Logs the client in to the server with the user of the storage, which a server with users needs
# for listing and restoring backups. The login lasts while the client stays connected to the
# server. Returns whether the storage has a user.
sub client_login {
    my ($class, $scfg, $storeid) = @_;

    my $username = $class->get_option($scfg, 'username');
    return 0 if !defined($username);

    # (the client takes the password from the environment)
    local $ENV{LOGIN_PASSWORD} = file_read_firstline(password_file($storeid)) // '';
    my ($rc) = $class->client_restore_cmd($scfg, 'login', { username => $username }, 300);
    die "unable to log in to the UrBackup server as '$username' - check the user name and the"
        . " password of storage '$storeid'\n" if $rc != 0;

    return 1;
}

# Runs a download command of the client (download_mbr, download_image), after logging in if the
# server asks for it. Returns its exit code and output.
sub client_download {
    my ($class, $scfg, $storeid, $cmd, $params, $timeout) = @_;

    my ($rc, $out, $err) = $class->client_restore_cmd($scfg, $cmd, $params, $timeout);
    # (the server sends no image: not logged in, or it does not have this image)
    if ($rc == 3 && $class->client_login($scfg, $storeid)) {
        ($rc, $out, $err) = $class->client_restore_cmd($scfg, $cmd, $params, $timeout);
    }

    return ($rc, $out, $err);
}

# Runs urbackupclientctl. Returns its exit code and output.
sub clientctl {
    my ($class, $scfg, $args, $timeout) = @_;

    my ($out, $err) = ('', '');
    my $rc = run_command(
        [$class->get_option($scfg, 'clientctl'), $args->@*],
        outfunc => sub { $out .= "$_[0]\n" },
        errfunc => sub { $err .= "$_[0]\n" },
        timeout => $timeout // 60,
        noerr => 1,
    );
    $err =~ s/\s+$//;

    return ($rc, $out, $err);
}

# The image backups of a guest on the UrBackup server, oldest first: { id, time, volume }
my sub server_images {
    my ($class, $scfg, $storeid, $vmid) = @_;

    my $allowed;
    for my $logged_in (0, 1) {
        my ($rc, $out, $err) = $class->client_restore_cmd(
            $scfg,
            'get_backupimages_json',
            { restore_name => $class->server_client_name($scfg, $vmid) },
            60,
        );
        if ($rc != 0) {
            die "unable to get the backups of guest $vmid from the UrBackup server"
                . " (urbackupclientbackend exit code $rc): $err\n";
        }
        return [] if $out =~ m/^\s*$/;

        my $images = eval { decode_json($out) };
        die "unexpected list of backups from the UrBackup client - $@" if $@;

        # The client asks the server on each of its connections to it (one per virtual client),
        # so the images can be listed more than once. The connections of the other virtual
        # clients are not allowed to see them, and none is if the server wants a login.
        $allowed = [grep { $_->{id} != 0 || $_->{letter} ne 'NO RIGHTS' } $images->@*];
        last if !$images->@* || $allowed->@*;

        die "the UrBackup server does not allow the client to list the backups of guest $vmid"
            . " (if the server has users, set the username and password of storage '$storeid')\n"
            if $logged_in || !$class->client_login($scfg, $storeid);
    }

    my $seen = {};

    return [
        sort { $a->{time} <=> $b->{time} || $a->{volume} cmp $b->{volume} }
        grep { !$seen->{"$_->{time} $_->{volume}"}++ }
        map {
            # (untainted, they become parameters of the download commands)
            my ($id) = ($_->{id} // '') =~ m/^(-?\d+)$/;
            my ($time) = ($_->{time_s} // '') =~ m/^(\d+)$/;
            my ($volume) = ($_->{letter} // '') =~ m/^([^\0\n]+)$/;
            defined($id) && defined($time) && defined($volume)
                ? { id => int($id), time => int($time), volume => $volume }
                : ();
        } $allowed->@*
    ];
}

# The files of the zip file the server keeps with the image of a whole disk: { name => content }.
# Nothing for an image of a volume: it has the partition table of the volume's disk there.
sub image_files {
    my ($class, $scfg, $storeid, $image) = @_;

    make_path($RUN_DIR);
    my $tmp = File::Temp->new(TEMPLATE => 'disk-info-XXXXXX', DIR => $RUN_DIR);
    my ($rc, $out, $err) = $class->client_download(
        $scfg,
        $storeid,
        'download_mbr',
        {
            restore_img_id => $image->{id},
            restore_time => $image->{time},
            restore_out => $tmp->filename,
        },
        300,
    );
    die "unable to get the disk information of image '$image->{volume}' from the UrBackup"
        . " server (urbackupclientbackend exit code $rc): $err\n" if $rc != 0;

    my $data = file_get_contents($tmp->filename);
    return undef if substr($data, 0, 2) ne "\x01\x64";
    $data = substr($data, 2);

    my $files = {};
    my $unzip = IO::Uncompress::Unzip->new(\$data)
        or die "unable to read the disk information of image '$image->{volume}' - $UnzipError\n";
    my $status;
    do {
        my $name = $unzip->getHeaderInfo()->{Name};
        local $/ = undef;
        $files->{$name} = $unzip->getline() // '';
    } while (($status = $unzip->nextStream()) > 0);
    die "unable to read the disk information of image '$image->{volume}' - $UnzipError\n"
        if $status < 0;

    return $files;
}

# The backup job an image belongs to: { backup-time, device, size, devices }, as the backup
# provider stored it with the image (see write_disk_info there). Empty for other images.
my sub image_backup_info {
    my ($class, $scfg, $storeid, $image) = @_;

    my $files = $class->image_files($scfg, $storeid, $image) // return {};
    my $disk = eval { decode_json($files->{'disk.json'} // '') };
    return {} if ref($disk) ne 'HASH';
    return {} if ($disk->{'backup-time'} // '') !~ m/^\d+$/;
    return {} if ($disk->{device} // '') !~ m/^[\w.-]+$/;

    my $info = {
        'backup-time' => int($disk->{'backup-time'}),
        device => $disk->{device},
        size => ($disk->{size} // '') =~ m/^\d+$/ ? int($disk->{size}) : 0,
    };
    # (images of the first versions of the plugin do not have the disks of the guest)
    $info->{devices} = [map { "$_" } $disk->{devices}->@*] if ref($disk->{devices}) eq 'ARRAY';
    return $info;
}

# The backups of a VM, oldest first: { type => 'qemu', time, size, images => [{ id, time,
# volume }] }, with the size of the VM's disks as the size.
#
# A backup is the images of the guest's disks that the server made during one backup job. The
# server does not know about the jobs, so the backup provider stores the time of the job and the
# disks of the guest with each image. A backup is listed when the server has an image of each
# of its disks. The node remembers what it read from the server: it does not change, and
# getting it takes a download per image.
sub vm_backups {
    my ($class, $scfg, $storeid, $vmid) = @_;

    my $images = server_images($class, $scfg, $storeid, $vmid);

    my $json = JSON->new->canonical;
    my $cache_fn = $class->state_dir() . "/$vmid.images";
    my $cached = -e $cache_fn ? file_get_contents($cache_fn) : '{}';
    my $cache = eval { $json->decode($cached) };
    $cache = {} if ref($cache) ne 'HASH';

    my $infos = {}; # of the images the server still has
    my $backups = {}; # time of the job => { time, devices, images => { device => image } }
    for my $image ($images->@*) {
        my $key = "$image->{time} $image->{volume}";
        my $info = $cache->{$key};
        # (not there yet, or from a version of the plugin that did not keep the size)
        if (ref($info) ne 'HASH' || ($info->%* && !defined($info->{size}))) {
            $info = eval { image_backup_info($class, $scfg, $storeid, $image) };
            if (my $err = $@) {
                warn $err;
                next;
            }
        }
        $infos->{$key} = $info;
        next if !$info->%*;

        my $time = $info->{'backup-time'};
        my $backup = $backups->{$time} //= { time => int($time), images => {} };
        # (the newest image, if the server has more than one of a disk from this job)
        $backup->{images}->{ $info->{device} } = $image;
        $backup->{sizes}->{ $info->{device} } = $info->{size};
        $backup->{devices} = $info->{devices};
    }

    my $new_cached = $json->encode($infos);
    if ($new_cached ne $cached) {
        eval {
            make_path($class->state_dir());
            file_set_contents($cache_fn, $new_cached);
        };
        warn $@ if $@;
    }

    my $complete = sub {
        my ($backup) = @_;
        return !grep { !$backup->{images}->{$_} } ($backup->{devices} // [])->@*;
    };

    return [
        map {
            my $size = 0;
            $size += $_ for values $_->{sizes}->%*;
            {
                type => 'qemu',
                time => $_->{time},
                size => $size,
                images => [sort { $a->{volume} cmp $b->{volume} } values $_->{images}->%*],
            }
        }
        grep { $complete->($_) }
        sort { $a->{time} <=> $b->{time} } values $backups->%*
    ];
}

# The backups of a container, oldest first: { type => 'lxc', time, size, id }, with the size the
# server tells for its file backup.
#
# A backup is a file backup of the guest's virtual client that has the configuration of the
# container next to its file system (see backup_container of the backup provider). There, the
# name of a file tells the time of the backup job. The client may list and restore its own file
# backups without a login.
sub container_backups {
    my ($class, $scfg, $vmid) = @_;

    my $vc = "pve-$vmid";
    my ($rc, $out, $err) = $class->clientctl($scfg, ['browse', '--virtual-client', $vc]);
    # The client has no access tokens before its first file backup, and the server only says
    # "err" if it has no file backups of the virtual client that the client may access (e.g.
    # the one of a VM)
    return [] if $rc != 0 && $err =~ m/No file backup access tokens|^Error getting file backups$/;
    die "unable to get the file backups of guest $vmid from the UrBackup server"
        . " (urbackupclientctl exit code $rc): $err\n" if $rc != 0;

    my $file_backups = eval { decode_json($out) };
    die "unexpected list of file backups from the UrBackup client - $@" if $@;

    my $json = JSON->new->canonical;
    my $cache_fn = $class->state_dir() . "/$vmid.files";
    my $cached = -e $cache_fn ? file_get_contents($cache_fn) : '{}';
    my $cache = eval { $json->decode($cached) };
    $cache = {} if ref($cache) ne 'HASH';

    my $times = {}; # of the file backups the server still has: time of the job, 0: not a backup
    my $backups = {};
    for my $file_backup ($file_backups->@*) {
        my ($id) = ($file_backup->{id} // '') =~ m/^(\d+)$/ or next;
        my ($server_time) = ($file_backup->{backuptime} // '') =~ m/^(\d+)$/ or next;
        my $key = "$id $server_time";

        my $time = $cache->{$key};
        if (!defined($time)) {
            ($rc, $out, $err) = $class->clientctl(
                $scfg,
                [
                    'browse', '--virtual-client', $vc, '--backupid', $id,
                    '--path', $class->container_config_name($vmid),
                ],
            );
            my $files = $rc == 0 ? eval { decode_json($out) } : undef;
            if (ref($files) ne 'ARRAY') {
                # (a file backup of other directories does not have it: nothing to read then)
                next if $rc == 0 || $err !~ m/Error getting file list$/;
                $files = [];
            }
            ($time) = map { ($_->{name} // '') =~ m/^job-(\d+)$/ ? $1 : () } $files->@*;
            $time //= 0;
        }
        $times->{$key} = int($time);
        next if !$time;

        # (the newest one, if the server has more than one from this job)
        $backups->{$time} = {
            type => 'lxc',
            time => int($time),
            size => ($file_backup->{size_bytes} // '') =~ m/^(\d+)$/ ? int($1) : 0,
            id => int($id),
        } if !$backups->{$time} || $backups->{$time}->{id} < $id;
    }

    my $new_cached = $json->encode($times);
    if ($new_cached ne $cached) {
        eval {
            make_path($class->state_dir());
            file_set_contents($cache_fn, $new_cached);
        };
        warn $@ if $@;
    }

    return [sort { $a->{time} <=> $b->{time} } values $backups->%*];
}

# The backups of a guest, oldest first
sub guest_backups {
    my ($class, $scfg, $storeid, $vmid) = @_;

    return [
        sort { $a->{time} <=> $b->{time} } $class->vm_backups($scfg, $storeid, $vmid)->@*,
        $class->container_backups($scfg, $vmid)->@*,
    ];
}

# The guests this node backs up: the virtual clients "pve-<vmid>" of its UrBackup client
my sub backed_up_guests {
    my ($class, $scfg) = @_;

    my $defaults_fn = $class->get_option($scfg, 'client-dir') . "/data/client_defaults.cfg";
    return () if !-e $defaults_fn;

    my ($virtual_clients) = file_get_contents($defaults_fn) =~ m/^virtual_clients_add=(.*)$/m;
    return sort { $a <=> $b } map { m/^pve-(\d+)$/ ? $1 : () } split(/\|/, $virtual_clients // '');
}

# The notes of the backups of a guest: { "<qemu|lxc>-<backup time>" => notes }. The server has
# no place for them, so they are kept on this node.
my $MAX_NOTES = 1000;

my sub notes_file {
    my ($class, $vmid) = @_;
    return $class->state_dir() . "/$vmid.notes";
}

my sub read_notes {
    my ($class, $vmid) = @_;

    my $fn = notes_file($class, $vmid);
    my $notes = -e $fn ? eval { JSON->new->utf8->decode(file_get_contents($fn)) } : undef;
    return ref($notes) eq 'HASH' ? $notes : {};
}

# Storage implementation

# Volume names: backup/<vmid>/<qemu|lxc>-<backup time>
sub parse_volname {
    my ($class, $volname) = @_;

    if ($volname =~ m!^backup/((\d+)/(?:qemu|lxc)-\d+)$!) {
        return ('backup', $1, $2);
    }

    die "unable to parse volume name '$volname'\n";
}

sub path {
    my ($class, $scfg, $volname, $storeid, $snapname) = @_;
    die "volume snapshots are not possible on UrBackup storage\n" if $snapname;
    my ($type, $name, $vmid) = $class->parse_volname($volname);
    return ("urbackup://$name", $vmid, $type);
}

sub create_base {
    die "cannot create base images on UrBackup storage\n";
}

sub clone_image {
    die "cannot clone images on UrBackup storage\n";
}

sub alloc_image {
    die "cannot allocate images on UrBackup storage\n";
}

# The server removes the backups, as its settings say. A client cannot remove backups there, so
# for Proxmox VE all backups are protected.
sub free_image {
    die "backups are removed by the UrBackup server, as its settings say\n";
}

sub list_images {
    return [];
}

sub list_volumes {
    my ($class, $storeid, $scfg, $vmid, $content_types) = @_;

    return [] if !grep { $_ eq 'backup' } $content_types->@*;

    my $res = [];
    for my $guest (defined($vmid) ? ($vmid) : backed_up_guests($class, $scfg)) {
        my $notes = read_notes($class, $guest);
        for my $backup ($class->guest_backups($scfg, $storeid, $guest)->@*) {
            my $backup_notes = $notes->{"$backup->{type}-$backup->{time}"};
            push @$res, {
                volid => "$storeid:backup/$guest/$backup->{type}-$backup->{time}",
                content => 'backup',
                format => 'raw',
                # (of a VM, the size of the disks: the server does not tell how much space the
                # images take)
                size => $backup->{size},
                ctime => 0 + $backup->{time},
                vmid => int($guest),
                subtype => $backup->{type},
                protected => 1,
                defined($backup_notes) ? (notes => $backup_notes) : (),
            };
        }
    }

    return $res;
}

# Nothing to prune: the server removes the backups
sub prune_backups {
    my ($class, $scfg, $storeid, $keep, $vmid, $type, $dryrun, $logfunc) = @_;

    return [
        map {
            {
                ctime => $_->{ctime},
                type => $_->{subtype},
                vmid => $_->{vmid},
                volid => $_->{volid},
                mark => 'protected',
            }
        }
        grep { !defined($type) || $_->{subtype} eq $type }
        $class->list_volumes($storeid, $scfg, $vmid, ['backup'])->@*
    ];
}

sub status {
    my ($class, $storeid, $scfg, $cache) = @_;
    # The space is managed by the UrBackup server
    return (0, 0, 0, 1);
}

sub activate_storage {
    my ($class, $storeid, $scfg, $cache) = @_;

    my $clientctl = $class->get_option($scfg, 'clientctl');
    die "urbackupclientctl not found at '$clientctl'\n" if !-x $clientctl;

    my $client_dir = $class->get_option($scfg, 'client-dir');
    die "UrBackup client data directory '$client_dir' does not exist\n" if !-d "$client_dir/data";

    return 1;
}

sub deactivate_storage {
    return 1;
}

sub activate_volume {
    my ($class, $storeid, $scfg, $volname, $snapname, $cache) = @_;
    die "volume snapshots are not possible on UrBackup storage\n" if $snapname;
    return 1;
}

sub deactivate_volume {
    return 1;
}

sub get_volume_attribute {
    my ($class, $scfg, $storeid, $volname, $attribute) = @_;

    return 1 if $attribute eq 'protected';

    if ($attribute eq 'notes') {
        my (undef, $name, $vmid) = $class->parse_volname($volname);
        return read_notes($class, $vmid)->{ $name =~ s!^.*/!!r };
    }

    return;
}

sub update_volume_attribute {
    my ($class, $scfg, $storeid, $volname, $attribute, $value) = @_;

    # (vzdump sets it for a backup job with the option "protected")
    if ($attribute eq 'protected') {
        die "backups are removed by the UrBackup server, as its settings say - they cannot be"
            . " unprotected\n" if !$value;
        return;
    }
    die "attribute '$attribute' is not supported on UrBackup storage\n" if $attribute ne 'notes';

    my (undef, $name, $vmid) = $class->parse_volname($volname);
    # (in statements of their own: vzdump runs with -T, and the notes taint what else a
    # statement makes)
    my $fn = notes_file($class, $vmid);
    my $key = $name =~ s!^.*/!!r;

    my $notes = read_notes($class, $vmid);
    if (defined($value) && $value ne '') {
        $notes->{$key} = $value;
    } else {
        delete $notes->{$key};
    }
    # (the notes of the newest backups, should there ever be that many)
    my $time = sub { $_[0] =~ m/-(\d+)$/ ? $1 : 0 };
    my @keys = sort { $time->($b) <=> $time->($a) } keys $notes->%*;
    delete $notes->@{ @keys[$MAX_NOTES .. $#keys] } if @keys > $MAX_NOTES;

    make_path($class->state_dir());
    file_set_contents($fn, JSON->new->utf8->canonical->encode($notes));

    return;
}

sub volume_size_info {
    my ($class, $scfg, $storeid, $volname, $timeout) = @_;

    my (undef, $name, $vmid) = $class->parse_volname($volname);
    my ($type, $time) = $name =~ m!/(\w+)-(\d+)$!;

    my $backups = $type eq 'lxc'
        ? $class->container_backups($scfg, $vmid)
        : $class->vm_backups($scfg, $storeid, $vmid);
    my ($backup) = grep { $_->{time} == $time } $backups->@*;
    die "backup '$volname' not found on the UrBackup server\n" if !$backup;

    # (Proxmox VE takes a volume without a size as one that does not exist)
    my $size = $backup->{size} || 1;
    return wantarray ? ($size, 'raw', $size, undef, $backup->{time}) : $size;
}

sub volume_resize {
    die "volume resize is not possible on UrBackup storage\n";
}

sub volume_snapshot {
    die "volume snapshots are not possible on UrBackup storage\n";
}

sub volume_snapshot_rollback {
    die "volume snapshots are not possible on UrBackup storage\n";
}

sub volume_snapshot_delete {
    die "volume snapshots are not possible on UrBackup storage\n";
}

sub volume_has_feature {
    return 0;
}

sub new_backup_provider {
    my ($class, $scfg, $storeid, $log_function) = @_;

    return PVE::BackupProvider::Plugin::UrBackup->new($class, $scfg, $storeid, $log_function);
}

1;
