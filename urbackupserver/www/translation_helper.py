import re
import codecs
import collections
import os
import polib

def write_po_header(f, lang):
    
    f.writelines(["# UrBackup translation\n",
                  "# Copyright (C) 2011-2014 See translators\n",
                  "# This file is distributed under the same license as the UrBackup package.\n",
                  "msgid \"\"\n",
                  "msgstr \"\"\n",
                  "\"MIME-Version: 1.0\\n\"\n",
                  "\"Content-Type: text/plain; charset=UTF-8\\n\"\n",
                  "\"Content-Transfer-Encoding: 8bit\\n\"\n",
                  "\"Language: "+lang+"\\n\"\n",
                  "\n"]); 

def po_from_translations():
    with codecs.open("js/translation.js", "r", encoding="utf8") as translation_file:
        
        translation = translation_file.readlines();
        
        lang = ""
        
        translations = {};
        
        for line in translation:
            
            m = re.search("translations\.([A-Za-z_]*)[ ]*=[ ]*{", line, 0)
            
            if m:
                
                lang = m.group(1)
                
                translations[lang] = collections.OrderedDict()
            
            
            m = re.search("\"(.*?)\".*:.*\"(.*)\"", line, 0)
            
            if m and len(lang)>0:
                
                key = m.group(1)
                val = m.group(2)
                
                translations[lang][key] = val
                
                
        enkeys = collections.OrderedDict()  
        
        for lang in translations:
            
            with codecs.open(lang+".po" ,"w", encoding="utf8") as pofile:
                
                write_po_header(pofile, lang)
                
                trans = translations[lang]
            
                for key in trans:
                    
                    pofile.writelines(["msgid \""+key+"\"\n",
                                       "msgstr \""+trans[key]+"\"\n",
                                       "\n"])
                                       
                    enkeys[key] = True
                    
        templ_dir = r"templates"
        skip = ["target_db_version", "time" ]
        for file in os.listdir(templ_dir):
            if file.endswith(".htm"):
                
                with codecs.open(templ_dir+"\\"+file ,"r", encoding="utf8") as templ:
                    
                    for line in templ.readlines():
                        
                        for m in re.finditer("{t([^\|]*?)(\|.*?)?}", line):
                            
                            key = m.group(1)
                            
                            if key not in enkeys and (not m.group(2) or "s" not in m.group(2)):
                                
                                if "t"+key not in skip:
                                    enkeys["t"+key] = True
    
                    
        with codecs.open("en.po" ,"w", encoding="utf8") as pofile:
            
            write_po_header(pofile, "en")
            
            for key in enkeys:    
                if key in translations["en"]:
                    
                    trans = translations["en"][key];
                    
                    pofile.writelines(["msgid \""+key+"\"\n",
                                       "msgstr \""+trans+"\"\n",
                                       "\n"])
                                       
                elif len(key)>0 and key[0]=="t":
                    
                    pofile.writelines(["msgid \""+key+"\"\n",
                                       "msgstr \""+key[1:]+"\"\n",
                                       "\n"])
            
def translations_from_po():
    
    lang_map = { "fa_IR": "fa" }
    
    with codecs.open("js/translation.js", "w", encoding="utf8") as output:
        
        output.write("if(!window.translations) translations=new Object();\n")
            
        tdir = "translations/urbackup.webinterface"
        
        prev_lang = False
        
        for file in os.listdir(tdir):
            if file.endswith(".po"):
                
                m = re.search("([A-Za-z_]*).*\.po", file);
                
                if m:                    
                    lang = m.group(1)
                    
                    po = polib.pofile(tdir + "/" + file)
                    
                    if len(po.translated_entries())>0:
                                            
                        if prev_lang:
                            output.write("\n}\n")
                            
                        prev_lang = True
                        
                        if lang in lang_map:
                            lang = lang_map[lang]
                                            
                        output.write("translations."+lang+" = {\n")                          
                        
                        
                        prev_entry = False     
                    
                        for entry in po.translated_entries():
                            
                            if prev_entry:
                                output.write(",\n")
                                
                            prev_entry = True
                            
                            msgid = entry.msgid.replace("\"", "\\\"")
                            msgstr = entry.msgstr.replace("\"", "\\\"")
                            msgstr = msgstr.replace("\n", "\\n")
                            
                            output.write("\""+msgid+"\": \""+msgstr+"\"")
        
        if prev_lang:
            output.write("\n}\n")
                        

translations_from_po() 
    
                                   
                        