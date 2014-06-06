import pbs
import http.client as http
from urllib.parse import urlparse
import json
import uuid
import re

git = pbs.Command("git")

def get_branch():
    binfo = git("branch")
    m = re.search("\*[ ]*([^ \r\n]*)", binfo.stdout, 0)
    if m:
        return m.group(1)
    else:
        return "master"
    
def get_head_rev():
    head_rev = git("rev-parse", "HEAD")
    return head_rev.stdout[:10]
    

def get_version_load(branch):
    
    target = urlparse("http://buildserver.urbackup.org/urbackup_build_version_"+branch+".json")
    method = 'GET'
    body = ''
    
    headers = {
            'Accept': 'application/json',
            'Content-Type': 'application/json; charset=UTF-8'
        }
    
    http_timeout = 10*60;
    
    if(target.scheme=='http'):
        h = http.HTTPConnection(target.hostname, target.port, timeout=http_timeout)
    elif(target.scheme=='https'):
        h = http.HTTPSConnection(target.hostname, target.port, timeout=http_timeout)
    else:
        print('Unkown scheme: '+target.scheme)
        raise Exception("Unkown scheme: "+target.scheme)
    
    h.request(
            method,
            target.path+"?"+target.query,
            body,
            headers)
    
    return h.getresponse();

def get_version(branch):     
    tries = 50
    
    while tries>0:
        response = get_version_load(branch)
    
        if(response.status == 200):
            break
        
        tries=tries-1
        if(tries==0):
            return None
        else:
            print("API call failed. Retrying...")
    
    data = response.readall();
    
    response.close()
        
    return json.loads(data.decode("utf-8"))

def replace_in_file(fn, to_replace, new_str):
    newlines = []
    with open(fn,'r') as f:
        for line in f.readlines():
            newlines.append(line.replace(to_replace, new_str))
            
    with open(fn, 'w') as f:
        for line in newlines:
            f.write(line)


version = get_version(get_branch())

if version == None:
    exit(1)

version["server"]["full_rev"] = version["server"]["full"] + " Rev. " + get_head_rev();

server_short_files = ["urbackupserver/www/index.htm",
                      "urbackupserver_installer_win/urbackup_server.nsi"]

for short_file in server_short_files:
    replace_in_file(short_file, "$version_short$", version["server"]["short"])
      
    
replace_in_file("urbackupserver/www/index.htm", "$version_full$", version["server"]["full_rev"])
replace_in_file("urbackupserver/www/index.htm", "$version_full$", version["server"]["full_rev"])
replace_in_file("urbackupserver_installer_win/urbackup_server.wxs", "$version_full$", version["server"]["full"])
replace_in_file("urbackupserver_installer_win/urbackup_server.wxi", "$product_id$", str(uuid.uuid1()))


if os.path.exists("client"):
	client_short_files = ["client/urbackup.nsi",
						  "client/urbackup_update.nsi",
						  "client/urbackup_notray.nsi"]

	for short_file in client_short_files:
		replace_in_file(short_file, "$version_short$", version["client"]["short"])


	replace_in_file("client/urbackup.wxs", "$version_full$", version["client"]["full"])
	replace_in_file("client/urbackup.wxi", "$product_id$", uuid.uuid1())

exit(0)
