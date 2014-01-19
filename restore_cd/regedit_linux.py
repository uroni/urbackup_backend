from subprocess import *;
import re

class keylist_item:
    name = ""
    value = 0;
    key = True
    
    def __str__(self):
        return "name="+self.name+" value="+str(self.value)+" key="+str(self.key)
    
    def __repr__(self):
        return self.__str__();


class regedit_linux:

    _hive_filename=""
   
    def __init__(self, hive_filename):
        self._hive_filename=hive_filename;
    
    
    def _regeditExecute(self, cmds):
        
        cmd="";
        
        has_quit=False
        for c in cmds:
            
            cmd+=c+"\n"
            
            if c=="q":
                has_quit=True
            
        
        if not has_quit:
            cmd+="q\n";
        
        p1 = Popen(["chntpw", "-e", self._hive_filename], stdin=PIPE, stdout=PIPE, close_fds=True)
        
        out, _ = p1.communicate(cmd.encode())
        
        return out.decode()        
        
        
    def _parseList(self, listStr):
        res = re.search(r"Node has \d+ subkeys and \d+ values\s*(.*)>\s*Hives that have changed", listStr, re.MULTILINE|re.DOTALL);
        
        #print("Match:###### group0")
        #print(res.group(0))   
        #print("Match:###### group1")
        #print(res.group(1))       
        
        keyList = res.group(1);
        
        strKeyList = re.findall(r"(.*<.*>.*\s)", keyList);
        
        retList = {}
        
        for key in strKeyList:
            
            item = keylist_item()
            
            valG = re.search(r"(.*)<(.*)>\s*(\d*)\s*", key);
            
            
            if len(valG.group(2))>0:
                item.name=valG.group(2);
                
            if len(valG.group(3))>0:
                item.value=int(valG.group(3))
                item.key=False
                
            if "REG_" in valG.group(1) :
                item.key=False
            
            retList[item.name]=item
            
        return retList
    
    def list(self, path):
        
              
        ret = self._regeditExecute(["ls "+path])
               
        return self._parseList(ret);
        
        
    def value(self, path, key):
        
        ret = self._regeditExecute(["cd "+path, "cat "+key])
        
        res = re.search(r"Value <"+key+"> of type.*?\n(.*?)\n", ret, re.MULTILINE|re.DOTALL)
        
        if res:        
            return res.group(1)
        else:
            return None
        
    def edit(self, path, key, newValue):
        
        ret = self._regeditExecute(["cd "+path, "ed "+key, newValue, "q", "y"])
        
        if "New value "+newValue in ret:
            return True
        else:
            return False
        
        