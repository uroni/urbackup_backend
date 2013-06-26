if not exist client git clone -b next http://urpc.dyndns.org/git/urbackup_frontend_wx client

cd client

git fetch
git checkout -b feature-quota origin/feature-quota
git reset --hard
git pull
