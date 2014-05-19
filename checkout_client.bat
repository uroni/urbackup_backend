if not exist client git clone -b next http://buildserver.urbackup.org/git/urbackup_frontend_wx client

cd client

git fetch
git checkout -b next origin/next
git checkout next
git reset --hard
git pull
