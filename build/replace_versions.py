import pbs
import http.client as http
from urllib.parse import urlparse
import json
import uuid
import re
import os
import hashlib

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
    build_info_url = "http://buildserver.urbackup.org/urbackup_build_version_"+branch+".json"
    print("Build info url: "+build_info_url)
    target = urlparse(build_info_url)
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
        raise Exception("Unknown scheme: "+target.scheme)
    
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
            print("Getting version failed. Retrying...")
    
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
         
def get_content_hash(fn):
    return hashlib.md5(open(fn, 'rb').read()).hexdigest()   
            
            
content_hash_replacements = {}

def replace_with_content_hashes_line(line, fn_prefix):
    global content_hash_replacements
    
    regex = ""
    if line.lower().find("<link rel=\"stylesheet\" type=\"text/css\"")!=-1:
        regex = r"href=\"(.*?)\""
        
    if line.lower().find("<script language=\"javascript\" src=\"")!=-1:
        regex = r"src=\"(.*?)\""
    
    if len(regex)>0:
        m = re.search(regex, line)
        if m:
            fn = m.group(1)
            
            if not os.path.exists(fn_prefix + fn) and fn in content_hash_replacements:
                new_fn = content_hash_replacements[fn]
                return line.replace(fn, new_fn)
            
            h = get_content_hash(fn_prefix + fn)
            if line.find(h)==-1:
                l = fn.split(".")
                l.insert(1, "chash-" + h)
                new_fn = ".".join(l)
                content_hash_replacements[fn] = new_fn
                os.rename(fn_prefix + fn, fn_prefix + new_fn)
                return line.replace(fn, new_fn)
            
    return line
            

def replace_with_content_hashes(fn, fn_prefix):
    newlines = []
    with open(fn,'r') as f:
        for line in f.readlines():
            newlines.append(replace_with_content_hashes_line(line, fn_prefix))
            
    with open(fn, 'w') as f:
        for line in newlines:
            f.write(line)


replace_with_content_hashes("urbackupserver/www/index.htm", "urbackupserver/www/")

version = get_version(get_branch())

if version == None:
    exit(1)

version["server"]["full_rev"] = version["server"]["full"] + " Rev. " + get_head_rev();

server_short_files = ["urbackupserver/www/index.htm",
                      "urbackupserver_installer_win/urbackup_server.nsi",
					  "urbackupserver_installer_win/generate_msi.bat"]

for short_file in server_short_files:
    replace_in_file(short_file, "$version_short$", version["server"]["short"])
      
    
replace_in_file("urbackupserver/www/index.htm", "$version_full$", version["server"]["full_rev"])
replace_in_file("urbackupserver_installer_win/urbackup_server.wxs", "$version_full_numeric$", version["server"]["full_numeric"])
replace_in_file("urbackupserver_installer_win/urbackup_server.wxi", "$product_id$", str(uuid.uuid1()))
if os.path.exists("client"):
	client_short_files = ["client_version.h",
						  "client/urbackup.nsi",
						  "client/urbackup_update.nsi",
						  "client/urbackup_notray.nsi",
						  "client/build_msi.bat",
						  "client/build_client.bat",
						  "osx_installer/resources/welcome.html",
						  "create_osx_installer.sh",
						  "install_client_linux.sh"]

	for short_file in client_short_files:
		replace_in_file(short_file, "$version_short$", version["client"]["short"])

	version_maj = version["client"]["full_numeric"].split(".")[0]
	version_min = int(version["client"]["full_numeric"].split(".")[1])*1000+int(version["client"]["full_numeric"].split(".")[2])
	
	version_num_short = version["client"]["full_numeric"].split(".")[0] + "." + version["client"]["full_numeric"].split(".")[1] + "." + version["client"]["full_numeric"].split(".")[2]
	replace_in_file("osx_installer/info.plist", "$version_num_short$", version_num_short)
	replace_in_file("create_osx_installer.sh", "$version_num_short$", version_num_short)
	
	replace_in_file("osx_installer/info.plist", "$version_maj$", version_maj)
	replace_in_file("osx_installer/info.plist", "$version_min$", str(version_min))

	replace_in_file("client/urbackup.wxs", "$version_full_numeric$", version["client"]["full_numeric"])
	replace_in_file("client/urbackup.wxi", "$product_id$", str(uuid.uuid1()))
	replace_in_file("clientctl/main.cpp", "$version_full_numeric$", version["client"]["full_numeric"])

exit(0)
