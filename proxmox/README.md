# UrBackup backup provider for Proxmox VE

Backs up the virtual machines and containers of Proxmox VE with UrBackup, using the backup
provider API of Proxmox VE 9 (`vzdump --storage <urbackup storage>`).

The UrBackup client on the Proxmox VE host does the backups: each guest is a virtual client
`pve-<vmid>` of it.

Each disk of a VM is an image backup of its virtual client. During a backup, the plugin
connects the disks' backup exports to `/dev/nbdX` nodes and gives them stable names in
`/run/urbackup-pve/<vmid>/`. It then configures the client for the virtual client, asks the
server for an image backup of it with `urbackupclientctl` and waits until the client has sent
the image of each disk and the server has finished them. The images are images of the whole
disks. The server keeps a zip file with each of them that has the configuration of the VM, its
firewall configuration and the size of the disk. For a restore, the client downloads the
images of the backup to files in `restore-dir`, which Proxmox VE then imports into the storage
of the new VM.

While a VM runs, Proxmox VE keeps track of the parts of its disks that change after a backup
(bitmaps, as with the Proxmox Backup Server), and the next backup only reads those: see
"Reading only what has changed".

A container is a file backup of its virtual client: of its file system, which the plugin
mounts read-only at `/run/urbackup-pve/<vmid>/fs/rootfs` during the backup, and of a directory
with its configuration. The files are backed up with the user and group IDs the container
sees. For a restore, the client restores the files to a directory in `restore-dir`, from
which Proxmox VE copies them to the new container.

Removing backups from Proxmox VE is not possible: how long the server keeps the backups is a
setting of the server (and a client cannot remove backups there). Proxmox VE shows all backups
as protected, and the retention of a backup job removes nothing. The notes of the backups are
kept on the node (`/var/lib/urbackup-pve/<vmid>.notes`), other nodes of a cluster do not show
them. The size Proxmox VE shows for a backup of a VM is the size of its disks, not the space
the images take on the server.

## Requirements

- An UrBackup client on the host with image backup support for virtual clients (client
  defaults from `urbackup/data/client_defaults*.cfg`, `urbackupclientctl start --image`, the
  volume of finished image backups in `urbackupclientctl status`, images of whole disks with
  `urbackup/data/disk_<virtual client>_<disk>.zip`, the changed parts of a volume from its
  snapshot script with `CBT=type=extents`).
- An UrBackup server that does not start a second image backup of a volume while one is
  waiting or running.
- For VMs, the `nbd` kernel module with enough devices (`nbds_max=128`), which the installation
  sets up.
- For VMs, fleecing: Proxmox VE needs it for backup providers, e.g. in `/etc/vzdump.conf`:
  `fleecing: enabled=1,storage=local-lvm`.
- For unprivileged containers, a file system that can be mounted with other user and group
  IDs (`mount -o X-mount.idmap=...`), which the storages of Proxmox VE for containers can.

## Installation

As a Debian package, built on the host or elsewhere (`dpkg-deb`, `make`):

    make deb
    apt install ./build/urbackup-pve_*_all.deb

Or without a package:

    make install
    modprobe nbd
    systemctl restart pvedaemon pveproxy pvestatd pvescheduler

Both also install the configuration of the `nbd` kernel module in `/etc/modules-load.d` and
`/etc/modprobe.d`. Then add the storage, on each node of a cluster that has the plugin and an
UrBackup client (`--nodes`):

    pvesm add urbackup urbackup

## Storage options

| Option                | Default                            | Meaning |
|-----------------------|------------------------------------|---------|
| `client-dir`          | `/usr/local/var/urbackup`          | Data directory of the UrBackup client |
| `client-conf-dir`     | `/usr/local/etc/urbackup`          | Configuration directory of the client (`snapshot.cfg`) |
| `clientctl`           | `/usr/local/bin/urbackupclientctl` | Path of urbackupclientctl |
| `client-backend`      | `/usr/local/sbin/urbackupclientbackend` | Path of urbackupclientbackend |
| `client-name`         | computer name of the client, else the host name | Name of the host's client on the server |
| `restore-dir`         | `/var/tmp`                         | Directory for the disk images or files of a guest during a restore (needs space for up to the size of the guest) |
| `backup-timeout`      | 14400                              | Seconds to wait for the server to start and finish a backup |
| `full-image-interval` | 30                                 | Days after which the next backup of a VM is a full image backup |
| `username`, `password` | none                              | User of the UrBackup server for listing and restoring the backups of VMs, if the server has users |

The server's own backup schedule is turned off for the virtual clients, because the guests
can only be read during a backup job.

## Reading only what has changed

The first backup of a VM reads all of its disks. Proxmox VE then keeps a bitmap of the parts
of each disk that change, and the next backup only gets to read these parts. The UrBackup
client keeps the checksums of the blocks it has sent (`hdat_img_*.dat` in its data directory)
and tells the server about the rest of the disks from them.

- The server has to base an incremental image backup on the last image backup, not on the last
  full one (settings "Local/passive incremental image style" and "Internet/active incremental
  image style" of the client `<client>[pve-<vmid>]`: "Based on last image backup"). The plugin
  sets that for a virtual client that has no such setting on the server. If it is set
  otherwise, the backups read all of the disks, and their log says so.
- All of the disks are read as well (and compared with what the server has) when there is no
  bitmap of the changes since the last backup: after the VM was stopped or has moved to another
  node, after a backup that failed, after a disk was added or resized, and for a full image
  backup (`full-image-interval`).
- If the server has lost or removed the last image backup of a disk, the next backup fails
  (the client would have to read parts it cannot read), and the one after it reads all of the
  disks.
- The kernel may log `unable to read partition table` or `Other side returned error` for the
  `nbd` device of a disk during such a backup: it tries to read parts that have not changed.

## Containers

The file backups of a container are incremental after the first one. Mount points of the
container are part of the backup if they have the `backup` option, as with other backups of
Proxmox VE.

- Hard links become separate files, and sockets are not backed up.
- The server takes the files to exclude from the plugin when it gets to know the virtual
  client: the contents of `/tmp` and `/var/tmp`, `/var/run/*.pid` and `lost+found`. After
  that they are the setting "excluded files" of the client `<client>[pve-<vmid>]` on the
  server, as are the directories to back up. Other excluded paths of a backup job are not
  used.
- Proxmox VE restores the backup of a privileged container as an unprivileged one
  (`pct restore <vmid> <backup> --unprivileged 1`).
- The first backup of a new container waits a few minutes for the server to set up its virtual
  client.

## Clusters

Each node backs up its guests with its own UrBackup client: on the server, a guest is the
client `<client of the node>[pve-<vmid>]`. A node lists and restores the backups of the guests
it has backed up, also of guests that do not exist any more. After a guest has moved to another
node, its backups go on there as a new client on the server, starting with a full backup. The
earlier backups stay with the node the guest was on.

## Listing and restoring the backups

Proxmox VE shows the backups the server has of the guests this node backs up. The client
asks the server for them and downloads them for a restore.

It may list and restore its own file backups (containers). For image backups (VMs), a server
that has users only allows that after a login: set `username` and `password` of the storage
to a user of the server that may download the images of the guests' virtual clients
(`pvesm set <storage> --username <user> --password <password>`; the password is kept in
`/etc/pve/priv/storage/<storage>.pw`). Backups of VMs need the login as well, because the
plugin checks at the end of a backup that the server has the finished images. The server
counts a wrong password once per guest and refuses logins from the host for ten minutes after
more than ten failed ones.

If the connection to the server breaks during the restore of a VM, the client tries to
continue the download for ten minutes before the restore fails. Proxmox VE keeps the
configuration of a guest whose restore failed, without its disks; remove it or restore over
it.

The server keeps one image per disk and does not know which of them belong to one backup
job. The plugin stores the time of the job and the disks of the VM with each image, and
lists a backup when the server has the images of all its disks. The file backup of a
container has a file whose name is the time of the job. Reading that takes a small download
per image or file backup, so the node remembers it in `/var/lib/urbackup-pve/<vmid>.images`
and `<vmid>.files`; the files can be removed at any time.
