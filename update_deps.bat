if not exist deps git clone -b master http://buildserver.urbackup.org/git/urbackup_deps deps
cd deps
git reset --hard
git pull
cd ..