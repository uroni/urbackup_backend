g.main_nav_pos=5;
g.loading=false;
g.lang="en";
g.startup=true;
g.google_chart_loaded=false;
g.no_tab_mouse_click=false;
g.tabberidx=-1;
g.status_detail=false;
g.progress_stop_id=-1;

g.languages=[ { l: "English", s: "en" }, { l: "Deutsch", s: "de" } ];

function startup()
{
	if(!startLoading()) return;
	new getJSON("login", "", try_anonymous_login);
	
	if(g.use_google_chart)
	{
		LoadScript("https://www.google.com/jsapi?callback=google_chart_ready", "google_jsapi");
	}	
}

function refresh_page()
{
	if(g.last_action=="status")
	{
		show_status1();
	}
	else if(g.last_action=="progress")
	{
		show_progress1();
	}
	else
	{
		g.last_function(clone(g.last_data));
		build_main_nav();
	}
}

function change_lang(l, refresh)
{
	g.lang=l;
	if(l=="de")
	{
		I('load1').innerHTML="Lade...";
	}
	else
	{
		I('load1').innerHTML="Loading...";
	}
	
	var c="";
	for(var i=0;i<g.languages.length;++i)
	{
		if(g.languages[i].s==l)
		{
			c+="<strong>"+g.languages[i].l+"</strong>";
		}
		else
		{
			c+="<a href=\"javascript: change_lang('"+g.languages[i].s+"', true);\">"+g.languages[i].l+"</a>";
		}
		
		if(i+1<g.languages.length)
		{
			c+=" | ";
		}
	}
	
	I('languages').innerHTML=c;
	
	window.trans=window.translations[l];

	if(refresh)
	{
		refresh_page();
	}
}

function try_anonymous_login(data)
{
	stopLoading();
	
	if(g.startup)
	{
		var lang="en";
		if(data.lang=="de")
		{
			lang="de";
		}
		g.startup=false;
		change_lang(lang, false);
	}
	
	if(data.upgrading_database)
	{
		var ndata=tmpls.upgrade_error.evaluate(data);
		if(g.data_f!=ndata)
		{
			I('data_f').innerHTML=ndata;
			g.data_f=ndata;
		}
		return;
	}
	
	if(data.success)
	{
		g.session=data.session;
		build_main_nav();
		show_status1();
	}
	else
	{
		var ndata=tmpls.login.evaluate();
		if(g.data_f!=ndata)
		{
			I('data_f').innerHTML=ndata;
			g.data_f=ndata;
		}
		I('username').focus();
	}
}
function startLoading()
{
	if(g.loading)
		return false;
	
	I('l_div').style.visibility="visible";
	g.loading=true;
	return true;
}

function stopLoading()
{
	I('l_div').style.visibility="hidden";
	g.loading=false;
}

function google_chart_ready()
{
	google.load("visualization", "1", {packages:["corechart"], callback: chartLoaded});
}

function chartLoaded()
{
}

function build_main_nav()
{
	var ndata="";
	nav_items=["show_settings1", "show_statistics1", "show_logs1", "show_backups1", "show_progress1", "show_status1"];
	for(var i=0;i<nav_items.length;++i)
	{
		var found=false;
		if(!g.allowed_nav_items || g.allowed_nav_items.length==0)
			found=true;
		else
		{
			for(var j=0;j<g.allowed_nav_items.length;++j)
			{
				if(g.allowed_nav_items[j]==(i+1))
				{
					found=true;
					break;
				}
			}			
		}
		if(found)
		{
			var p="";
			if(g.nav_params && g.nav_params[i+1])
				p=g.nav_params[i+1];
			if(i+1==g.main_nav_pos)
			{
				ndata+=tmpls.main_nav_sel.evaluate({func: nav_items[i], name: trans["nav_item_"+(i+1)], params: p});
			}
			else
			{
				ndata+=tmpls.main_nav.evaluate({func: nav_items[i], name: trans["nav_item_"+(i+1)], params: p});
			}
		}
	}
	I('main_nav').innerHTML=ndata;
}

function getPar(p)
{
	var obj=I(p);
	if(!obj) return "";
	if(obj.type=="checkbox" )
	{
		return "&"+p+"="+(obj.checked?"true":"false");
	}
	var val=obj.value;
	if(p=="update_freq_incr") val*=60*60;
	if(p=="update_freq_full" || p=="update_freq_image_full" || p=="update_freq_image_incr") val*=60*60*24;
	if(p=="startup_backup_delay") val*=60;
	if(p=="update_freq_image_full" && I('client_disable_image_backups') && I('client_disable_image_backups').checked )
		val*=-1;
		
	return "&"+p+"="+encodeURIComponent(val+"");
}

function show_progress1(stop_clientid)
{
	if(!startLoading()) return;		
	show_progress11();
}
function show_progress11()
{
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=setTimeout(show_progress11, 10000);
	
	var pars="";
	if( g.progress_stop_id!=-1)
	{
		//alert(stop_clientid);
		pars="stop_clientid="+g.progress_stop_id;
		g.progress_stop_id=-1;
	}
	
	new getJSON("progress", pars, show_progress2);
	
	g.main_nav_pos=5;
	build_main_nav();
	I('nav_pos').innerHTML="";
}
function show_progress2(data)
{
	stopLoading();
	if(g.main_nav_pos!=5) return;
	
	var rows="";
	var tdata="";
	if(data.progress.length>0)
	{
		for(var i=0;i<data.progress.length;++i)
		{
			data.progress[i].action=trans["action_"+data.progress[i].action];
			rows+=tmpls.progress_row.evaluate(data.progress[i]);
		}
		tdata=tmpls.progress_table.evaluate({"rows": rows});
	}
	else
	{
		tdata=tmpls.progress_table_none.evaluate();
	}	
	
	if(data.lastacts.length>0)
	{
		rows="";
		for(var i=0;i<data.lastacts.length;++i)
		{
			var obj=data.lastacts[i];
			var action=0;
			if(obj.image==0)
			{
				if(obj.incremental>0)
					action=1;
				else
					action=2;
			}
			else
			{
				if(obj.incremental>0)
					action=3;
				else
					action=4;
			}
			var a="action_"+action;
			if(obj.del)
				a+="_d";
			obj.action=trans[a];
			if(obj.size_bytes==-1)
				obj.size=trans["unknown"];
			else
				obj.size=format_size(obj.size_bytes);
			if(obj.del)
				obj.size="-"+obj.size;
			
			obj.duration/=60;
			obj.duration=Math.ceil(obj.duration);
			obj.duration+=" min";
			
			rows+=tmpls.lastacts_row.evaluate(obj);
		}
		tdata+=tmpls.lastacts_table.evaluate({rows: rows});
	}
	
	if(g.data_f!=tdata)
	{
		I('data_f').innerHTML=tdata;
		g.data_f=tdata;
	}
	
	I('nav_pos').innerHTML="";
	
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=setTimeout(show_progress11, 1000);
}

function show_settings1()
{
}

function show_statistics1()
{	
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	new getJSON("users", "", show_statistics2);
	new getJSON("usage", "", show_statistics3);
	
	g.main_nav_pos=2;
	g.settings_nav_pos=0;
	build_main_nav();
}
function show_statistics2(data)
{
	stopLoading();
	if(g.main_nav_pos!=2) return;
	
	var ndata="<a href=\"javascript: show_statistics1()\">"+trans["overview"]+"</a>";
	if(g.settings_nav_pos==0)
	{
		ndata="<strong>"+trans["overview"]+"</strong>";
	}
	if(data.users.length>0)
	{		
		ndata+="&nbsp;| &nbsp;";
		ndata+="<select size=\"1\" style=\"width: 150px\" onchange=\"stat_client()\" id=\"statclient\">";
		if(g.settings_nav_pos<1)
		{
			ndata+="<option value=\"n\">"+trans["clients"]+"</option>";
		}
		for(var i=0;i<data.users.length;++i)
		{		
			s="";
			if(g.settings_nav_pos==i+1)
			{
				s=" selected=\"selected\"";
			}
			ndata+="<option value=\""+i+"\""+s+">"+data.users[i].name+"</option>";					
		}
		g.stat_data=data;
	}
	I('nav_pos').innerHTML=ndata;
}
function show_statistics3(data)
{
	stopLoading();
	if(g.main_nav_pos!=2) return;
	var ndata="";
	var rows="";
	var used_total=0;
	var files_total=0;
	var images_total=0;
	for(var i=0;i<data.usage.length;++i)
	{
		var obj=data.usage[i];
		used_total+=obj.used;
		files_total+=obj.files;
		images_total+=obj.images;
		obj.used=format_size(obj.used);
		obj.files=format_size(obj.files);
		obj.images=format_size(obj.images);
		rows+=tmpls.stat_general_row.evaluate(obj);
	}
	ndata=tmpls.stat_general.evaluate({rows: rows, used_total: format_size(used_total), files_total: format_size(files_total), images_total: format_size(images_total), ses: g.session});
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		new loadGraph("piegraph", "", "piegraph", {pie: true, width: 700, height: 700, 
			title: trans["storage_usage_pie_graph_title"], colname1: trans["storage_usage_pie_graph_colname1"], colname2: trans["storage_usage_pie_graph_colname2"] } );
		new loadGraph("usagegraph", "", "usagegraph", {pie: false, width: 800, height: 500, 
			title: trans["storage_usage_bar_graph_title"], colname1: trans["storage_usage_bar_graph_colname1"], colname2: trans["storage_usage_bar_graph_colname2"] });
		g.data_f=ndata;
	}
}

function stat_client(id, name)
{
	if(g.main_nav_pos!=2) return;
	
	var selidx=I('statclient').selectedIndex;
	if(selidx!=-1 && I('statclient').value!="n")
	{	
		var idx=I('statclient').value*1;
		var name=g.stat_data.users[idx].name;
		var id=g.stat_data.users[idx].id;
		g.settings_nav_pos=idx+1;
		g.data_f=tmpls.stat_user.evaluate({clientid: id, clientname: name, ses: g.session});
		I('data_f').innerHTML=g.data_f;
		new loadGraph("usagegraph", "clientid="+id, "usagegraph", {pie: false, width: 700, height: 700, 
			title: trans["storage_usage_pie_graph_title"], colname1: trans["storage_usage_pie_graph_colname1"], colname2: trans["storage_usage_pie_graph_colname2"] });
		show_statistics2(g.stat_data);
	}
}

function show_status1(details, hostname, remove, remove_client, stop_client_remove, start_backup_type)
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	var pars="";
	
	if(typeof details=="undefined")
		details=g.status_detail;
	else
		g.status_detail=details;
	if(details==true)
	{
		pars="details=true";
	}
	
	if(hostname && hostname.length>0)
	{
		if(pars!="")
		{
			pars+="&";
		}
		pars+="hostname="+hostname;
		
		if(remove)
		{
			pars+="&remove=true";
		}
	}
	if(start_backup_type && start_backup_type.length>0)
	{
		if(pars!="") pars+="&";
		pars+="start_client="+remove_client.join();
		pars+="&start_type="+start_backup_type;
	}
	else if(remove_client && remove_client.length>0)
	{
		if(pars!="") pars+="&";
		pars+="remove_client="+remove_client.join();
		
		if(stop_client_remove)
		{
			pars+="&stop_remove_client=true";
		}
	}
	new getJSON("status", pars, show_status2);
	
	g.main_nav_pos=6;
	build_main_nav();
	I('nav_pos').innerHTML="";
}
function show_status2(data)
{
	stopLoading();
	if(g.main_nav_pos!=6) return;
	
	var ndata="";
	var rows="";
	for(var i=0;i<data.status.length;++i)
	{
		var obj=data.status[i];
		if(obj.file_ok)
		{
			obj.file_style="background-color: green";
			obj.file_ok_t=trans["ok"];
		}
		else
		{
			obj.file_style="background-color: red";
			obj.file_ok_t=trans["no_recent_backup"];
		}
		
		if(obj.image_ok)
		{
			obj.image_style="background-color: green";
			obj.image_ok_t=trans["ok"];
		}
		else
		{
			obj.image_style="background-color: red";
			obj.image_ok_t=trans["no_recent_backup"];
		}
		
		if(obj.lastbackup=="") obj.lastbackup=trans["backup_never"];
		if(obj.lastbackup_image=="") obj.lastbackup_image=trans["backup_never"];
		
		if(obj.online) obj.online=trans["yes"];
		else obj.online=trans["no"];
		
		obj.Action_remove_start="";
		obj.Action_remove_end="";
		
		if(data.remove_client)
		{
			obj.prev_tab_class="tabFLeft";
			obj.Actions_start="";
			obj.Actions_end="";
			
			if(obj.id=="-")
			{
				obj.Action_remove_start="<!--";
				obj.Action_remove_end="-->";
			}
		}
		else
		{
			obj.prev_tab_class="tabFRight";
			obj.Actions_start="<!--";
			obj.Actions_end="-->";
		}
		
		obj.start_file_backup="";
		obj.start_image_backup="";
		
		if(obj.status>0 && obj.status<5)
		{
			if(obj.status>=1 && obj.status<=2)
			{
				obj.start_file_backup=trans["backup_in_progress"];
			}
			else
			{
				obj.start_image_backup=trans["backup_in_progress"];
			}
		}	
				
		switch(obj.status)
		{
			case 0: obj.status="ok"; break;
			case 1: obj.status="incr_file"; break;
			case 2: obj.status="full_file"; break;
			case 3: obj.status="incr_image"; break;
			case 4: obj.status="full_image"; break;
			case 10: obj.status=trans["starting"]; break;
			case 11: obj.status=trans["ident_err"]; break;
			case 12: obj.status=trans["too_many_clients_err"]; break;
		}
		
		if(data.allow_modify_clients)
		{
			obj.dtl_c1="";
			obj.dtl_c2="";
			obj.style_pre_last="tabFLeft";
		}
		else
		{
			obj.dtl_c1="<!--";
			obj.dtl_c2="-->";
			obj.style_pre_last="tabFRight";
		}		
		
		if(typeof obj.start_ok!="undefined")
		{
			if(obj.start_ok)
			{
				if( obj.start_type=="incr_file" || obj.start_type=="full_file" )
					obj.start_file_backup=trans["queued_backup"];
				else if( obj.start_type=="incr_image" || obj.start_type=="full_image" )
					obj.start_image_backup=trans["queued_backup"];
			}
			else
			{
				if( obj.start_type=="incr_file" || obj.start_type=="full_file" )
					obj.start_file_backup=trans["starting_backup_failed"];
				else if( obj.start_type=="incr_image" || obj.start_type=="full_image" )
					obj.start_image_backup=trans["starting_backup_failed"];
			}
		}
		
		if(obj.start_file_backup.length>0) obj.start_file_backup="<br />"+obj.start_file_backup;
		if(obj.start_image_backup.length>0) obj.start_image_backup="<br />"+obj.start_image_backup;
		
		if( obj.delete_pending && obj.delete_pending==1)
		{
			if(data.details)
			{
				if(data.allow_modify_clients)
					obj.colspan=9;
				else
					obj.colspan=8;
			}
			else
			{
				if(data.allow_modify_clients)
					obj.colspan=6;
				else
					obj.colspan=5;
			}
			
			if(data.remove_client)
			{
				obj.stop_remove_start="";
				obj.stop_remove_stop="";
			}
			else
			{
				obj.stop_remove_start="<!--";
				obj.stop_remove_stop="-->";
			}
			
			rows+=tmpls.status_row_delete_pending.evaluate(obj);
		}
		else
		{
			if(data.details)
			{
				rows+=tmpls.status_detail_row.evaluate(obj);
			}
			else
			{
				if(!obj.rejected)
				{
					rows+=tmpls.status_row.evaluate(obj);
				}
			}
		}
	}
	var dir_error="";
	if(data.dir_error)
	{
		dir_error=tmpls.dir_error.evaluate();
	}
	
	var tmpdir_error="";
	if(data.tmpdir_error)
	{
		tmpdir_error=tmpls.tmpdir_error.evaluate();
	}
	
	var extra_clients_rows="";
	
	if(data.extra_clients.length>0)
	{
		for(var i=0;i<data.extra_clients.length;++i)
		{
			var obj=data.extra_clients[i];
			
			if(obj.online) obj.online=trans["yes"];
			else obj.online=trans["no"];
			
			extra_clients_rows+=tmpls.status_detail_extra_row.evaluate(obj);
		}
	}
	else
	{
		extra_clients_rows=tmpls.status_detail_extra_empty.evaluate();
	}
	
	var c_tmpl=tmpls.status;
	
	if(data.details) c_tmpl=tmpls.status_detail;
	
	var dtl_c1="<!--";
	var dtl_c2="-->";
	
	if(data.allow_extra_clients)
	{
		dtl_c1="";
		dtl_c2="";
	}
	var modify_clients="";
	if(data.allow_modify_clients)
	{
		var rem_start="<!--";
		var rem_stop="-->";
		if(data.remove_client)
		{
			rem_start="";
			rem_stop="";
		}
		modify_clients=tmpls.status_modify_clients.evaluate({rem_start: rem_start, rem_stop: rem_stop});
	}
	
	var class_prev;
	var Actions_start;
	var Actions_end;
	if(data.remove_client)
	{
		class_prev="tabHeader";
		Actions_start="";
		Actions_end="";
	}
	else
	{
		class_prev="tabHeaderRight";
		Actions_start="<!--";
		Actions_end="-->";
	}
	
	ndata=c_tmpl.evaluate({rows: rows, ses: g.session, dir_error: dir_error, tmpdir_error: tmpdir_error,
		extra_clients_rows: extra_clients_rows, dtl_c1:dtl_c1, dtl_c2:dtl_c2, 
		class_prev:class_prev, Actions_start:Actions_start, Actions_end:Actions_end,
		server_identity: data.server_identity, modify_clients: modify_clients});
	
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
}
function addExtraClient()
{
	if(I('hostname').value.length==0)
	{
		alert(trans["enter_hostname"]);
		I('hostname').focus();
		return;
	}
	
	show_status1(true, I('hostname').value);
}

function removeExtraClient(id)
{
	show_status1(true, id+"", true);
}

function show_backups1()
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	new getJSON("backups", "", show_backups2);
	
	g.main_nav_pos=4;
	build_main_nav();
	I('nav_pos').innerHTML="";
}
function show_backups2(data)
{
	stopLoading();
	var ndata="";
	if(data.clients)
	{
		var rows="";
		for(var i=0;i<data.clients.length;++i)
		{
			var obj=data.clients[i];
			if(obj.lastbackup.length==0)
				obj.lastbackup="&nbsp;";
			rows+=tmpls.backups_clients_row.evaluate(obj);
		}
		ndata=tmpls.backups_clients.evaluate({rows: rows, ses: g.session});
	}
	else if(data.backups)
	{
		var rows="";
		for(var i=0;i<data.backups.length;++i)
		{
			var obj=data.backups[i];			
			obj.size_bytes=format_size(obj.size_bytes);
			obj.incr=obj.incremental>0;
			if( obj.incr )
				obj.incr=trans["yes"];
			else
				obj.incr=trans["no"];
			
			if(data.can_archive)
			{
				if( obj.archived>0 )
					obj.archived='<a href="#" onclick="unarchive_single('+obj.id+', '+data.clientid+'); return false;">☑</a>';
				else
					obj.archived='<a href="#" onclick="archive_single('+obj.id+', '+data.clientid+'); return false;">☐</a>';
			}
			else
			{
				if( obj.archived>0 )
					obj.archived='☑';
				else
					obj.archived='☐';
			}
			
			obj.clientid=data.clientid;
				
			rows+=tmpls.backups_backups_row.evaluate(obj);
		}
		ndata=tmpls.backups_backups.evaluate({rows: rows, ses: g.session, clientname: data.clientname, clientid: data.clientid});
	}
	else if(data.files)
	{
		var rows="";		
		var path=unescapeHTML(data.path);
		var els=path.split("/");
		var cp="";
		var curr_path="";
		
		var last_path="";
		for(var i=0;i<els.length-1;++i)
		{
			if(els[i].length>0)
			{
				last_path+="/"+els[i];
			}
		}
		
		if(els.length>1 && (els[1].length>0 || els.length>2))
		{
			cp+="<a href=\"javascript: tabMouseClickBackups("+data.clientid+", "+data.backupid+")\">"+data.backuptime+"</a> > ";
			rows+=tmpls.backups_files_row.evaluate({size:"&nbsp;", name:"..", proc:"Files", path: last_path, clientid: data.clientid, backupid:data.backupid});
		}
		else
		{
			cp+="<strong>"+data.backuptime+"</strong>"
		}
		
		for(var i=0;i<data.files.length;++i)
		{
			var obj=data.files[i];
			if(obj.dir)
			{
				obj.size="&nbsp;";
				obj.proc="Files";
			}
			else
			{
				obj.size=format_size(obj.size);				
				obj.proc="FilesDL";
			}
			obj.clientid=data.clientid;
			obj.backupid=data.backupid;
			obj.path=encodeURIComponent(path+"/"+obj.name).replace(/'/g,"%27");
				
			rows+=tmpls.backups_files_row.evaluate(obj);
		}
		
		for(var i=0;i<els.length;++i)
		{
			if(els[i].length>0)
			{
				curr_path+="/"+els[i];
				if(i+1<els.length)
				{
					cp+="<a href=\"javascript: tabMouseClickFiles("+data.clientid+","+data.backupid+",'"+(curr_path==""?"/":curr_path)+"')\">"+els[i]+"</a>";
					if(i!=0)
					{
						cp+=" > ";
					}
				}
				else
				{
					cp+="<strong>"+els[i]+"</strong>";
				}
			}
		}
		
		ndata=tmpls.backups_files.evaluate({rows: rows, ses: g.session, clientname: data.clientname, clientid: data.clientid, cpath: cp, backuptime: data.backuptime});
	}
	
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
}
function tabMouseOver(obj)
{
	g.mouse_over_styles=[];
	var idx=0;
	for(var i=0;i<obj.childNodes.length;++i)
	{
		if(obj.childNodes[i].style)
		{
			if(idx>0)
			{
				if(obj.childNodes[i].id!="nomouseover")
				{
					g.mouse_over_styles.push({backgroundColor: obj.childNodes[i].style.backgroundColor, color: obj.childNodes[i].style.color});
					obj.childNodes[i].style.backgroundColor='blue';
					if(obj.childNodes[i].childNodes.length>0 && obj.childNodes[i].childNodes[0].nodeType==3)
					{
						obj.childNodes[i].style.color='white';
					}
				}
			}
			else
			{
				obj.childNodes[i].innerHTML="<img src=\"arr.png\" />";
			}
			++idx;
		}
	}
}
function tabMouseOut(obj)
{
	var idx=0;
	var mos=0;
	for(var i=0;i<obj.childNodes.length;++i)
	{
		if(obj.childNodes[i].style)
		{
			if(idx>0)
			{
				if(typeof g.mouse_over_styles[mos]!="undefined")
				{
					obj.childNodes[i].style.backgroundColor=g.mouse_over_styles[mos].backgroundColor;
					obj.childNodes[i].style.color=g.mouse_over_styles[mos].color;
				}
				++mos;
			}
			else
			{
				obj.childNodes[i].innerHTML="&nbsp;";
			}
			++idx;
		}
	}
	if(mos>0)
	{
		g.mouse_over_styles.length=0;
	}
}
function tabMouseClickClients(clientid)
{
	if(!startLoading()) return;
	new getJSON("backups", "sa=backups&clientid="+clientid, show_backups2);
}
function tabMouseClickBackups(clientid, backupid)
{
	if(g.no_tab_mouse_click){ g.no_tab_mouse_click=false; return; }
	if(!startLoading()) return;
	new getJSON("backups", "sa=files&clientid="+clientid+"&backupid="+backupid+"&path=%2F", show_backups2);
}
function tabMouseClickFiles(clientid, backupid, path)
{
	if(!startLoading()) return;
	new getJSON("backups", "sa=files&clientid="+clientid+"&backupid="+backupid+"&path="+path.replace(/\//g,"%2F"), show_backups2);
}
function tabMouseClickFilesDL(clientid, backupid, path)
{
	location.href=getURL("backups", "sa=filesdl&clientid="+clientid+"&backupid="+backupid+"&path="+path.replace(/\//g,"%2F"));
}

function show_settings1()
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	new getJSON("settings", "", show_settings2);
	
	g.main_nav_pos=1;
	g.settings_nav_pos=0;
	build_main_nav();
	I('nav_pos').innerHTML="";
}
function show_settings2(data)
{
	stopLoading();
	if(data.navitems)
	{
		var n="";
		var nav=data.navitems;
		var idx=0;
		g.user_nav_pos_offset=0;
		g.mail_nav_pos_offset=0;
		g.internet_nav_pos_offset=0;
		if(nav.general)
		{
			if(g.settings_nav_pos==idx)
			{
				n+="<strong>"+trans["general_settings"]+"</strong>";
			}
			else
			{
				n+="<a href=\"javascript: generalSettings()\">"+trans["general_settings"]+"</a>";
			}
			++idx;
			++g.user_nav_pos_offset;
			++g.mail_nav_pos_offset;
			++g.internet_nav_pos_offset;
		}
		if(nav.mail)
		{
			if(n!="" ) n+=" | ";
			
			if(g.settings_nav_pos==idx)
			{
				n+="<strong>"+trans["mail_settings"]+"</strong>";
			}
			else
			{
				n+="<a href=\"javascript: mailSettings()\">"+trans["mail_settings"]+"</a>";
			}
			++idx;
			++g.user_nav_pos_offset;
			++g.internet_nav_pos_offset;
		}
		if(nav.internet)
		{
			if(n!="" ) n+=" | ";
			
			if(g.settings_nav_pos==idx)
			{
				n+="<strong>"+trans["internet_server_settings"]+"</strong>";
			}
			else
			{
				n+="<a href=\"javascript: internetSettings()\">"+trans["internet_server_settings"]+"</a>";
			}
			++g.user_nav_pos_offset;
			++idx;
		}
		if(nav.users)
		{	
			if(n!="" ) n+=" | ";
			
			if(g.settings_nav_pos==idx)
			{
				n+="<strong>"+trans["users"]+"</strong>";
			}
			else
			{
				n+="<a href=\"javascript: userSettings()\">"+trans["users"]+"</a>";
			}			
			++idx;
			++g.user_nav_pos_offset;
		}
		if(nav.clients)
		{
			g.settings_clients=nav.clients;
			
			if(nav.clients.length>0)
			{			
				if(n!="") n+=" | ";
				n+="<select size=\"1\" style=\"width: 150px\" onchange=\"clientSettings()\" id=\"settingsclient\">";
				if(g.settings_nav_pos<idx)
				{
					n+="<option value=\"n\">"+trans["clients"]+"</option>"
				}
				for(var i=0;i<nav.clients.length;++i)
				{		
					s="";
					if(g.settings_nav_pos==idx)
					{
						s=" selected=\"selected\"";
					}
					n+="<option value=\""+nav.clients[i].id+"-"+idx+"\""+s+">"+nav.clients[i].name+"</option>";					
					++idx;
				}
			}
		}
		I('nav_pos').innerHTML=n;
	}
	
	var ndata="";
	var tabber_set_idx=-1;
	if(data.sa)
	{
		if(data.sa=="general")
		{
			data.settings.no_images=getCheckboxValue(data.settings.no_images);
			data.settings.no_file_backups=getCheckboxValue(data.settings.no_file_backups);
			data.settings.allow_overwrite=getCheckboxValue(data.settings.allow_overwrite);
			data.settings.autoshutdown=getCheckboxValue(data.settings.autoshutdown);
			data.settings.autoupdate_clients=getCheckboxValue(data.settings.autoupdate_clients);
			data.settings.backup_database=getCheckboxValue(data.settings.backup_database);
			
			
			data.settings.allow_config_paths=getCheckboxValue(data.settings.allow_config_paths);
			data.settings.allow_starting_file_backups=getCheckboxValue(data.settings.allow_starting_file_backups);
			data.settings.allow_starting_image_backups=getCheckboxValue(data.settings.allow_starting_image_backups);
			data.settings.allow_pause=getCheckboxValue(data.settings.allow_pause);
			data.settings.allow_log_view=getCheckboxValue(data.settings.allow_log_view);
			
			
			data.settings.update_freq_incr/=60*60;
			data.settings.update_freq_full/=60*60*24;
			data.settings.update_freq_image_incr/=60*60*24;
			data.settings.update_freq_image_full/=60*60*24;
			data.settings.startup_backup_delay/=60;
			
			data.settings.no_compname_start="<!--";
			data.settings.no_compname_end="-->";
			
			data.settings.client_plural="s";
			
			data.settings.ONLY_WIN32_BEGIN=unescapeHTML(data.settings.ONLY_WIN32_BEGIN);
			data.settings.ONLY_WIN32_END=unescapeHTML(data.settings.ONLY_WIN32_END);
			
			data.settings.settings_inv=tmpls.settings_inv_row.evaluate(data.settings);
			ndata+=tmpls.settings_general.evaluate(data.settings);
			
			if(data.saved_ok)
			{
				ndata+=tmpls.settings_save_ok.evaluate();
				tabber_set_idx=g.tabberidx;
			}			
		}
		else if(data.sa=="clientsettings")
		{
			data.settings.overwrite=getCheckboxValue(data.settings.overwrite);
			data.settings.allow_overwrite=getCheckboxValue(data.settings.allow_overwrite);
			data.settings.allow_config_paths=getCheckboxValue(data.settings.allow_config_paths);
			data.settings.allow_starting_file_backups=getCheckboxValue(data.settings.allow_starting_file_backups);
			data.settings.allow_starting_image_backups=getCheckboxValue(data.settings.allow_starting_image_backups);
			data.settings.allow_pause=getCheckboxValue(data.settings.allow_pause);
			data.settings.allow_log_view=getCheckboxValue(data.settings.allow_log_view);
			data.settings.client_disable_image_backups=getCheckboxValue(data.settings.update_freq_image_full<0);
						
			if(data.settings.update_freq_image_full<0)
				data.settings.update_freq_image_full*=-1;
			
			data.settings.update_freq_incr/=60*60;
			data.settings.update_freq_full/=60*60*24;
			data.settings.update_freq_image_incr/=60*60*24;
			data.settings.update_freq_image_full/=60*60*24;
			data.settings.startup_backup_delay/=60;
			
			data.settings.no_compname_start="";
			data.settings.no_compname_end="";
						
			data.settings.settings_inv=tmpls.settings_inv_row.evaluate(data.settings);
			ndata+=tmpls.settings_user.evaluate(data.settings);
			
			if(data.saved_ok)
			{
				ndata+=tmpls.settings_save_ok.evaluate();
				tabber_set_idx=g.tabberidx;
			}
		}
		else if(data.sa=="mail")
		{
			if(data.settings.mail_ssl_only=="true") data.settings.mail_ssl_only="checked=\"checked\"";
			else data.settings.mail_ssl_only="";
			if(data.settings.mail_check_certificate=="true") data.settings.mail_check_certificate="checked=\"checked\"";
			else data.settings.mail_check_certificate="";
			
			ndata+=tmpls.settings_mail.evaluate(data.settings);
			
			if(data.saved_ok)
			{
				ndata+=tmpls.settings_save_ok.evaluate();
			}
			if(data.mail_test)
			{
				if(data.mail_test=="ok")
				{
					ndata+=tmpls.settings_mail_test_ok.evaluate();
				}
				else
				{
					ndata+=tmpls.settings_mail_test_failed.evaluate({mail_err: data.mail_test});
				}
			}
		}
		else if(data.sa=="internet")
		{
			if(data.settings.internet_full_file_backups=="true") data.settings.internet_full_file_backups="checked=\"checked\"";
			else data.settings.internet_full_file_backups="";
			if(data.settings.internet_image_backups=="true") data.settings.internet_image_backups="checked=\"checked\"";
			else data.settings.internet_image_backups="";

			ndata+=tmpls.settings_internet.evaluate(data.settings);
			if(data.saved_ok)
			{
				ndata+=tmpls.settings_save_ok.evaluate();
			}
		}
		else if(data.sa=="listusers")
		{
			if(data.add_ok)
			{
				ndata+=tmpls.settings_user_add_done.evaluate({msg: trans["user_add_done"] });
			}
			if(data.removeuser)
			{
				ndata+=tmpls.settings_user_add_done.evaluate({msg: trans["user_remove_done"] });
			}
			if(data.update_right)
			{
				ndata+=tmpls.settings_user_add_done.evaluate({msg: trans["user_update_right_done"] });
			}
			if(data.change_ok)
			{
				ndata+=tmpls.settings_user_add_done.evaluate({msg: trans["user_pw_change_ok"] });
			}
			
			
			if(data.alread_exists)
			{
				alert(trans["user_exists"]);
				return;
			}
		
			var rows="";
			if(data.users.length>0)
			{
				g.user_rights={};
				for(var i=0;i<data.users.length;++i)
				{
					var obj=data.users[i];
					
					var t_rights=trans["user"];
					
					g.user_rights[obj.id]=obj.rights;
					
					for(var j=0;j<obj.rights.length;++j)
					{
						var right=obj.rights[j];
						if(right.domain=="all" && right.right=="all")
						{
							t_rights=trans["admin"];
						}
					}
					
					obj.rights=t_rights;
					
					rows+=tmpls.settings_users_start_row.evaluate(obj);
				}
			}
			else
			{
				rows=tmpls.settings_users_start_row_empty.evaluate();
			}
			g.num_users=data.users.length;
			ndata+=tmpls.settings_users_start.evaluate({ rows:rows });
		}
	}
	
	g.archive_item_id=0;
	
	var update_tabber=false;
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
		update_tabber=true;
	}
	
	if(data.sa && data.sa=="clientsettings")
	{
		updateUserOverwrite();
	}
	
	if(update_tabber)
	{
		var tabberOptions = { manualStartup:true,
								'onClick': function(argsObj) {
									g.tabberidx = argsObj.index; } };
		tabberAutomatic(tabberOptions);
		if(tabber_set_idx!=-1)
		{
			I('settings_tabber').tabber.tabShow(tabber_set_idx);
		}
		else
		{
			g.tabberidx=-1;
		}
	}
	
	if(update_tabber && data.sa && (data.sa=="clientsettings" || data.sa=="general") )
	{
		for(var i=0;i<data.archive_settings.length;++i)
		{
			var obj=data.archive_settings[i];
			addArchiveItemInt(getTimelengthUnit(obj.archive_every, obj.archive_every_unit), obj.archive_every_unit,
					getTimelengthUnit(obj.archive_for, obj.archive_for_unit), obj.archive_for_unit, obj.archive_backup_type, obj.next_archival);
		}
	}
}

g.settings_list=[
"update_freq_incr",
"update_freq_full",
"update_freq_image_full",
"update_freq_image_incr",
"max_file_incr",
"min_file_incr",
"max_file_full",
"min_file_full",
"min_image_incr",
"max_image_incr",
"min_image_full",
"max_image_full",
"allow_overwrite",
"startup_backup_delay",
"backup_window",
"computername",
"exclude_files",
"include_files",
"default_dirs",
"allow_config_paths",
"allow_starting_file_backups",
"allow_starting_image_backups",
"allow_pause",
"allow_log_view",
"image_letters",
"internet_authkey"
];
g.mail_settings_list=[
"mail_servername",
"mail_serverport",
"mail_username",
"mail_password",
"mail_from",
"mail_ssl_only",
"mail_check_certificate"
];
g.internet_settings_list=[
"internet_server",
"internet_server_port"
];

function validateCommonSettings()
{
	if(!validate_text_int(["update_freq_incr", "update_freq_full", "update_freq_image_incr", 
							"update_freq_image_full", "max_file_incr", "min_file_incr", "max_file_full", 
							"min_file_full", "max_image_incr", "min_image_incr", "max_image_full", "min_image_full",
							"startup_backup_delay"] ) ) return false;
	if(!validate_text_regex([{ id: "backup_window", regexp: /^(([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]\-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?\-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*[;]?\s*)*$/i }]) ) return false;
	if(!validate_text_regex([{ id: "image_letters", regexp: /([A-Za-z][;,])*/i }] ) ) return false;
	return true;
}
function getArchivePars()
{
	var pars="";
	for(var i=0;i<g.archive_item_id;++i)
	{
		pars+=getPar("archive_next_"+i);
		pars+=getPar("archive_every_"+i);
		pars+=getPar("archive_every_unit_"+i);
		pars+=getPar("archive_for_"+i);
		pars+=getPar("archive_for_unit_"+i);
		pars+=getPar("archive_backup_type_"+i);
	}
	return pars;
}
function saveGeneralSettings()
{
	if(!validate_text_nonempty(["backupfolder"]) ) return;
	if(!validate_text_int(["max_sim_backups", "max_active_clients"]) ) return;
	if(!validateCommonSettings() ) return;
	if(!validate_text_regex([{ id: "cleanup_window", regexp: /^(([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]\-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?\-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*[;]?\s*)*$/i }]) ) return;
	if(!startLoading()) return;
			
	var pars="";
	pars+=getPar("backupfolder");
	pars+=getPar("no_images");
	pars+=getPar("no_file_backups");
	pars+=getPar("autoshutdown");
	pars+=getPar("autoupdate_clients");
	pars+=getPar("max_sim_backups");
	pars+=getPar("max_active_clients");
	pars+=getPar("tmpdir");
	pars+=getPar("cleanup_window");
	pars+=getPar("backup_database");
	pars+=getArchivePars();
	for(var i=0;i<g.settings_list.length;++i)
	{
		pars+=getPar(g.settings_list[i]);
	}
	new getJSON("settings", "sa=general_save"+pars, show_settings2);
}
function saveMailSettings()
{	
	if(!startLoading()) return;
	var pars="";
	for(var i=0;i<g.mail_settings_list.length;++i)
	{
		pars+=getPar(g.mail_settings_list[i]);
	}
	pars+=getPar("testmailaddr");
	new getJSON("settings", "sa=mail_save"+pars, show_settings2);
}
function saveInternetSettings()
{	
	if(!validate_text_int(["internet_server_port"]) ) return;
	if(!startLoading()) return;
	var pars="";
	for(var i=0;i<g.internet_settings_list.length;++i)
	{
		pars+=getPar(g.internet_settings_list[i]);
	}
	new getJSON("settings", "sa=internet_save"+pars, show_settings2);
}
function clientSettings()
{
	var selidx=I('settingsclient').selectedIndex;
	if(selidx!=-1 && I('settingsclient').value!="n")
	{
		if(!startLoading()) return;
		clientid=I('settingsclient').value.split("-")[0];
		idx=I('settingsclient').value.split("-")[1];
		g.settings_nav_pos=idx*1;
		new getJSON("settings", "sa=clientsettings&t_clientid="+clientid, show_settings2);
	}
}
function generalSettings()
{
	if(!startLoading()) return;
	g.settings_nav_pos=0;
	new getJSON("settings", "sa=general", show_settings2);
}
function mailSettings()
{
	if(!startLoading()) return;
	g.settings_nav_pos=g.mail_nav_pos_offset;
	new getJSON("settings", "sa=mail", show_settings2);
}
function internetSettings()
{
	if(!startLoading()) return;
	g.settings_nav_pos=g.internet_nav_pos_offset;
	new getJSON("settings", "sa=internet", show_settings2);
}
function updateUserOverwrite(clientid)
{
	var checked=I('overwrite').checked;
	
	for(var i=0;i<g.settings_list.length;++i)
	{
		I(g.settings_list[i]).disabled=!checked;
	}
	
	I('user_submit').disabled=!checked;
	
	//Archive
	I('archive_add').disabled=!checked;
	I('archive_every').disabled=!checked;
	I('archive_for').disabled=!checked;
	I('archive_every_unit').disabled=!checked;
	I('archive_for_unit').disabled=!checked;
	I('archive_backup_type').disabled=!checked;
	
	if(clientid)
	{
		saveClientSettings(clientid, true);
	}
}
function saveClientSettings(clientid, skip)
{
	if(!startLoading()) return;
	var pars="";
	pars+=getPar("overwrite");
	if(!skip)
	{
		if(!validateCommonSettings())
		{
			stopLoading();
			return;
		}
		pars+=getArchivePars();
		for(var i=0;i<g.settings_list.length;++i)
		{
			pars+=getPar(g.settings_list[i]);
		}
	}
	else
	{
		pars+="&no_ok=true";
	}
	new getJSON("settings", "sa=clientsettings_save&t_clientid="+clientid+pars, show_settings2);
}
function userSettings()
{
	if(!startLoading()) return;
	g.settings_nav_pos=g.user_nav_pos_offset-1;
	new getJSON("settings", "sa=listusers", show_settings2);
}
function createUser()
{
	var d="";
	if(g.num_users==0)
		d="disabled=\"disabled\"";
		
	var rights="<select id=\"rights\" size=\"1\" style=\"width: 250px\" "+d+">";
	rights+="<option value=\"-1\">"+trans["admin"]+"</option>";
	
	for(var i=0;i<g.settings_clients.length;++i)
	{
		var obj=g.settings_clients[i];
		rights+="<option value=\""+obj.id+"\">"+obj.name+"</option>";
	}
	
	rights+="</select>";
	
	var ndata="";
	if(g.num_users==0)
		ndata=tmpls.settings_user_create_admin.evaluate({ rights: rights });
	else
		ndata=tmpls.settings_user_create.evaluate({ rights: rights });
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	
	if(g.num_users==0)
	{
		I('password1').focus();
	}
	else
	{
		I('username').focus();
	}
}
function generateRightsParam(t_rights)
{
	var r="";
	var idx="";
	for(var i=0;i<t_rights.length;++i)
	{
		if(i!=0)
			r+="&";
		r+=i+"_domain="+t_rights[i].domain;
		r+="&"+i+"_right="+t_rights[i].right;
		idx+=(i+"");
		if(i+1<t_rights.length)
		{
			idx+=",";
		}
	}
	r+="&idx="+idx;
	return encodeURIComponent(r);
}
function adminRights()
{
	return ([ { domain: "all", right: "all" } ]);
}

function clientRights(clientid)
{
	return (
	[
		{ domain: "browse_backups", right: clientid },
		{ domain: "lastacts", right: clientid },
		{ domain: "progress", right: clientid },
		{ domain: "settings", right: clientid },
		{ domain: "status", right: clientid },
		{ domain: "logs", right: clientid }
	]);
}
function createUser2()
{
	var username=I('username').value;
	var password1=I('password1').value;
	var password2=I('password2').value;
	
	if( username.length==0 )
	{	
		alert(trans["username_empty"]);
		I('username').focus();
		return;
	}
	
	if( password1.length==0 )
	{
		alert(trans["password_empty"]);
		I('password1').focus();
		return;
	}
	
	if( password1!=password2 )
	{
		alert(trans["password_differ"]);
		I('password1').focus();
		return;
	}
	
	var salt=randomString();	
	var password_md5=calcMD5(salt+password1);
	
	var t_rights;
	var cid=I('rights').value;
	if(cid==0 || cid==-1)
	{
		t_rights=adminRights();
	}
	else
	{
		t_rights=clientRights(cid);
	}
	
	var pars="&name="+username+"&pwmd5="+password_md5+"&salt="+salt+"&rights="+generateRightsParam(t_rights);
	
	if(!startLoading()) return;
	new getJSON("settings", "sa=useradd"+pars, show_settings2);
}
g.login1=function ()
{
	var username=I('username').value;
	var password=I('password').value;
	
	if( username.length==0 )
	{	
		alert(trans["username_empty"]);
		I('username').focus();
		return false;
	}
	if( password.length==0 )
	{
		alert(trans["password_empty"]);
		I('password').focus();
		return false;
	}
	
	if(!startLoading()) return false;
	
	new getJSON("salt", "username="+username, login2);
	
	return false;
}
function login2(data)
{
	if(data.error==0)
	{
		alert(trans["user_n_exist"]);
		stopLoading();
		I('username').focus();
		return;
	}
	
	if(data.ses)
		g.session=data.ses;
	
	var username=I('username').value;
	var password=I('password').value;
	
	var pwmd5=calcMD5(data.rnd+calcMD5(data.salt+password));
	
	new getJSON("login", "username="+username+"&password="+pwmd5, login3);
}
function login3(data)
{
	stopLoading();
	if(data.error==2)
	{
		alert(trans["password_wrong"]);
		I('password').focus();
		return;
	}
	
	g.allowed_nav_items = [];
	if(data.status!="none")
	{
		g.allowed_nav_items.push(6);
	}
	if(data.progress!="none")
	{
		g.allowed_nav_items.push(5);
	}
	if(data.browse_backups!="none")
	{
		g.allowed_nav_items.push(4);
	}
	if(data.logs!="none")
	{
		g.allowed_nav_items.push(3);
	}
	if(data.graph!="none")
	{
		g.allowed_nav_items.push(2);
	}
	if(data.settings!="none")
	{
		g.allowed_nav_items.push(1);
	}
	
	build_main_nav();
	show_status1();
}
g.session_timeout_cb = function ()
{
	clearTimeout(g.refresh_timeout);
	stopLoading();
	alert(trans["session_timeout"]);
	I('main_nav').innerHTML="";
	I('nav_pos').innerHTML="";
	g.session="";
	startup();
}
function deleteUser(uid)
{
	var c=confirm(trans["really_del_user"]);
	if(c)
	{
		if(!startLoading()) return;
		new getJSON("settings", "sa=removeuser&userid="+uid, show_settings2);
	}
}
function changeUserPassword(uid, name)
{
	var ndata=tmpls.settings_user_pw_change.evaluate({userid: uid, username: name});
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	I('password1').focus();
}
function changeUserPW(uid)
{	
	var password1=I('password1').value;
	var password2=I('password2').value;
	
	if( password1.length==0 )
	{
		alert(trans["password_empty"]);
		I('password1').focus();
		return;
	}
	
	if( password1!=password2 )
	{
		alert(trans["password_differ"]);
		I('password1').focus();
		return;
	}
	
	var salt=randomString();
	var password_md5=calcMD5(salt+password1);
	
	var pars="&userid="+uid+"&pwmd5="+password_md5+"&salt="+salt;
	
	if(!startLoading()) return;
	new getJSON("settings", "sa=changepw"+pars, show_settings2);
}
function transRights()
{
	var n=0;
	while(true)
	{
		var right=I('right'+n);
		var right_trans=I('right_trans'+n);
		if( right!=null && right_trans!=null )
		{
			var t="";
			if(right.value=="all")
			{
				t=trans["right_all"];
			}
			else if(right.value=="none")
			{
				t=trans["right_none"];
			}
			else
			{
				var s=right.value.split(",");
				for(var j=0;j<s.length;++j)
				{
					var f=false;
					var fn="";
					for(var k=0;k<g.settings_clients.length;++k)
					{
						if(g.settings_clients[k].id==s[j])
						{
							fn=g.settings_clients[k].name;
							f=true;
							break;
						}
					}
					
					if(f)
					{
						if(t.length>0)t+=",";
						t+=fn;
					}
				}
			}
			right_trans.value=t;
			
		}
		else
		{
			break;
		}
		++n;
	}
}

function changeUserRights(uid, name)
{
	var rows="";
	for(var i=0;i<g.user_rights[uid].length;++i)
	{
		var obj=g.user_rights[uid][i];
		obj.userid=uid;
		obj.username=name;
		obj.n=i;
		
		
		rows+=tmpls.settings_user_rights_change_row.evaluate(obj);
	}
	var ndata=tmpls.settings_user_rights_change.evaluate({userid: uid, username: name, rows: rows});
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	transRights();
}
function deleteDomain(uid, name, n)
{
	g.user_rights[uid].splice(n,1);
	changeUserRights(uid, name);
}
function addNewDomain(uid, name)
{
	obj={ domain: "", right: ""};
	g.user_rights[uid].push(obj);
	changeUserRights(uid, name);
}
function submitChangeUserRights(uid)
{
	if(!startLoading()) return false;
	
	var n=0;
	var rights=[];
	while(true)
	{
		var right=I('right'+n);
		var domain=I('domain'+n);
		if( right!=null && domain!=null )
		{
			rights.push( { right: right.value, domain: domain.value } );
		}
		else
		{
			break;
		}
		++n;
	}
	
	new getJSON("settings", "sa=updaterights&userid="+uid+"&rights="+generateRightsParam(rights), show_settings2);
}

function saveReportSettings()
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	
	logs_add_mail();
	
	var params="d=d";
	params+=getPar("report_mail");
	params+=getPar("report_sendonly");
	params+=getPar("report_loglevel");
	
	new getJSON("logs", params, show_logs2);
}

function show_logs1(params)
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	if(!params)params="";
	new getJSON("logs", params, show_logs2);
	
	g.main_nav_pos=3;
	build_main_nav();
	I('nav_pos').innerHTML="";
}

function show_logs2(data)
{
	stopLoading();
	
	if(data.clients && !data.log)
	{
		var np=trans["filter"]+": ";
		np+="<select size=\"1\" onchange=\"logClientChange()\" id=\"logclients\">";
		np+="<option value=\"-1\">"+trans["all"]+"</option>";
		for(var i=0;i<data.clients.length;++i)
		{
			var obj=data.clients[i];
			var c="";
			if(data.filter && obj.id==data.filter)
			{
				c="selected=\"selected\"";
			}
			np+="<option value=\""+obj.id+"\" "+c+">";
			np+=obj.name;
			np+="</option>";
		}
		np+="</select> ";
		np+=tmpls.logs_filter.evaluate();
		
		
		I('nav_pos').innerHTML=np;
		I('logsfilter').selectedIndex=2-data.ll;
	}
	else
	{
		var np=tmpls.log_single_filter.evaluate();
		I('nav_pos').innerHTML=np;
	}
	
	var ndata="";
	
	if(data.logs)
	{
		var rows="";
		for(var i=0;i<data.logs.length;++i)
		{
			var obj=data.logs[i];
			
			if(obj.errors>0)
				obj.estyle="background-color: red";
			
			if(obj.warnings>0)
				obj.wstyle="background-color: yellow";
				
			var action=0;
			if(obj.image==0)
			{
				if(obj.incremental>0)
					action=1;
				else
					action=2;
			}
			else
			{
				if(obj.incremental>0)
					action=3;
				else
					action=4;
			}
			var a="action_"+action;
			
			obj.action=trans[a];
			
			rows+=tmpls.logs_row.evaluate(obj);
		}
		if(data.logs.length==0)
			rows=tmpls.logs_none.evaluate();
			
			
		var sel="selected=\"selected\"";
		
		var td={};
		td.rows=rows;
		td.report_mail=data.report_mail;
		td.sel_all=(data.report_sendonly==0)?sel:"";
		td.sel_failed=(data.report_sendonly==1)?sel:"";
		td.sel_succ=(data.report_sendonly==2)?sel:"";
		td.sel_info=(data.report_loglevel==0)?sel:"";
		td.sel_warn=(data.report_loglevel==1)?sel:"";
		td.sel_error=(data.report_loglevel==2)?sel:"";
			
		ndata+=tmpls.logs_table.evaluate(td);
	}
	
	if(data.log)
	{
		g.logdata=data.log.data;
		var ll=2;
		if(g.has_logfilter)
			ll=g.logfilter;
		var rows=createLog(g.logdata,ll);
		if(rows=="")
			rows=tmpls.log_single_none.evaluate();
		g.logclientname=data.log.clientname;
		var params="";
		if(g.has_logsfilter)
			params+="ll="+g.logsfilter;
		if(g.has_logclients && g.logclients!=-1)
		{
			if(params.length>0) params+="&";
			params+="filter="+g.logclients;
		}
		ndata+=tmpls.log_single.evaluate({rows:rows, name: data.log.clientname, params: params});
	}
	
	if(data.saved_ok)
	{
		ndata+=tmpls.settings_save_ok.evaluate();
	}	
	
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	
	if(g.has_logfilter && I('logfilter'))
	{
		I('logfilter').selectedIndex=2-g.logfilter;
	}
	
	if(data.logs)
	{
		logs_draw_mail();
	}
}
function logs_draw_mail()
{
	var d="";
	var a=I('report_mail').value.split(';');
	for(var i=0;i<a.length;++i)
	{
		if(a[i]!="")
		{
			d+=tmpls.logs_report_mail.evaluate( {report_single_mail: a[i], num: i} );
		}
	}
	I('s_report_mails').innerHTML=d;
}
function logs_add_mail()
{
	if(I('report_new_mail').value!='')
	{
		if( I('report_mail').value=='' )
		{
			I('report_mail').value=I('report_new_mail').value;
		}
		else
		{
			I('report_mail').value+=';'+I('report_new_mail').value;
		}
		I('report_new_mail').value="";
		logs_draw_mail();
	}
}
function logs_rm_mail(idx)
{
	var a=I('report_mail').value.split(';');
	var n="";
	for(var i=0;i<a.length;++i)
	{
		if(n.length>0) n+=";";
		
		if(i!=idx)
		{
			n+=a[i];
		}
	}
	I('report_mail').value=n;
	logs_draw_mail();
}
function createLog(d, ll)
{
	var msgs=d.split("\n");
	var rows="";
	for(var i=0;i<msgs.length;++i)
	{
		var obj={};
		obj.level=msgs[i].substr(0,1);
		obj.message=msgs[i].substr(2, msgs[i].length-2);
		obj.time="-";
		
		if(obj.level>=ll && obj.message.length>0)
		{		
			var idx=obj.message.indexOf("-");
			if(idx!=-1)
			{
				obj.time=obj.message.substr(0,idx);
				if(!isNaN(obj.time-0))
				{
					var d=new Date(obj.time*1000);
					obj.time=format_date(d);
					obj.message=obj.message.substr(idx+1,obj.message.length-idx-1);
				}
				else
				{
					obj.time="-";
				}
			}
			
			if(obj.level==1)
				obj.lstyle="background-color: yellow";
			else if(obj.level==2)
				obj.lstyle="background-color: red";
				
			obj.level=trans["loglevel_"+obj.level];
			
			rows+=tmpls.log_single_row.evaluate(obj);
		}
	}
	return rows;
}
function logClientChange()
{
	var v=I('logclients').value;
	g.has_logclients=true;
	g.logclients=v;
	if(v==-1)
	{
		if(!startLoading()) return;
		new getJSON("logs", "ll="+I('logsfilter').value, show_logs2);
	}
	else
	{
		if(!startLoading()) return;
		new getJSON("logs", "filter="+v+"&ll="+I('logsfilter').value, show_logs2);
	}
	updateLogsParam();
}
g.tabMouseClickLogs=function(logid)
{
	if(!startLoading()) return;
	new getJSON("logs", "logid="+logid, show_logs2);
}
function logFilterChange()
{
	var v=I('logfilter').value;
	
	g.has_logfilter=true;
	g.logfilter=v;
	
	var rows=createLog(g.logdata,v);
	if(rows=="")
			rows=tmpls.log_single_none.evaluate();
	var ndata=tmpls.log_single.evaluate({rows:rows, name: g.logclientname});
	
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
}
function logsFilterChange()
{
	var v=I('logsfilter').value;
	var v2=I('logclients').value;
	
	g.has_logsfilter=true;
	g.logsfilter=v;
	
	if(v2==-1)
	{
		if(!startLoading()) return;
		new getJSON("logs", "ll="+v, show_logs2);
	}
	else
	{
		if(!startLoading()) return;
		new getJSON("logs", "filter="+v2+"&ll="+v, show_logs2);
	}
	updateLogsParam();
}
function updateLogsParam()
{
	var p="";
	if(g.has_logsfilter)
	{
		p="ll="+g.logsfilter;
	}
	if(g.has_logclients && g.logclients!=-1)
	{
		if(p.length>0) p+="&";
		p+="filter="+g.logclients;
	}
	if(!g.nav_params)
		g.nav_params={};
	g.nav_params[3]=p;
	build_main_nav();
}
function removeClient(clientid)
{
	if(!startLoading()) return;
	
	var b=confirm(trans["really_remove_client"]);
	if(b)
	{
		show_status1(g.status_detail, "", false, [clientid]);
	}
}
function removeClients()
{
	if(!startLoading()) return;
	
	var cbs=document.getElementsByName("status_selected");
	var ids=[];
	for(var i=0;i<cbs.length;++i)
	{
		if(cbs[i].checked)
		{
			ids.push(cbs[i].value);
		}
	}
	if(ids.length==1)
	{
		removeClient(cbs[0].value);
		return;
	}
	else if(ids.length>0)
	{	
		var b=confirm(trans["really_remove_clients"]);
		if(b)
		{
			show_status1(g.status_detail, "", false, ids);
		}
	}
	else
	{
		alert(trans["no_client_selected"]);
	}
}
function selectAllClients()
{
	var cbs=document.getElementsByName("status_selected");
	for(var i=0;i<cbs.length;++i)
	{
		cbs[i].checked=true;
	}
}
function selectNoClients()
{
	var cbs=document.getElementsByName("status_selected");
	for(var i=0;i<cbs.length;++i)
	{
		cbs[i].checked=false;
	}
}
function stopRemove(clientid)
{
	if(!startLoading()) return;
	show_status1(g.status_detail, "", false, [clientid], true);
}
function unarchive_single(backupid, clientid)
{
	if(!startLoading()) return;
	new getJSON("backups", "sa=backups&clientid="+clientid+"&unarchive="+backupid, show_backups2);
}
function archive_single(backupid, clientid)
{
	if(!startLoading()) return;
	new getJSON("backups", "sa=backups&clientid="+clientid+"&archive="+backupid, show_backups2);
}
function addArchiveItem()
{
	validate_text_nonempty(["archive_every", "archive_for"]);
	addArchiveItemInt(parseInt(I('archive_every').value), I('archive_every_unit').value, parseInt(I('archive_for').value), I('archive_for_unit').value, I('archive_backup_type').value, -1);
}
function getTimelengthSeconds(tl, unit)
{
	tl*=60*60;
	if(unit!='h')
	{
		tl*=24;
		if(unit!='d')
		{
			tl*=7;
			if(unit!='w')
			{
				tl*=4.345;
				if(unit!='m')
				{
					tl*=12;
				}
			}
		}
	}
	return tl;
}
function getTimelengthUnit(tl, unit)
{
	tl/=3600;
	if(unit!='h')
	{
		tl/=24;
		if(unit!='d')
		{
			tl/=7;
			if(unit!='w')
			{
				tl/=4.345;
				if(unit!='m')
				{
					tl/=12;
				}
			}
		}
	}
	return tl;
}
function addPlural(val, str)
{
	if(val!=1)
		return str+"s";
	else
		return str;
}
function dectorateTimelength(tl, unit)
{
	if(unit=='h') tl+=" "+trans[addPlural(tl, "hour")];
	else if(unit=='d') tl+=" "+trans[addPlural(tl,"day")];
	else if(unit=='w') tl+=" "+trans[addPlural(tl,"week")];
	else if(unit=='m') tl+=" "+trans[addPlural(tl,"month")];
	else if(unit=='y') tl+=" "+trans[addPlural(tl,"year")];
	else if(unit=='i') tl=trans["forever"];
	return  tl;
}
function backupTypeStr(bt)
{
	if(bt=="incr_file") return trans["action_1"];
	else if(bt=="full_file") return trans["action_2"];
	else if(bt=="file") return trans["file_backup"];
}
function getArchiveTable()
{
	archive_table=I('archive_table');
	
	if(archive_table.childNodes.length<=2)
	{
		for(var i=0;i<archive_table.childNodes.length;++i)
		{
			if(archive_table.childNodes[i].childNodes.length>2)
			{
				archive_table=archive_table.childNodes[i];
				break;
			}
		}
	}
	return archive_table;
}
function addArchiveItemInt(archive_every, archive_every_unit, archive_for, archive_for_unit, archive_backup_type, next_archival)
{
	archive_every_i=getTimelengthSeconds(archive_every, archive_every_unit);
	archive_for_i=getTimelengthSeconds(archive_for, archive_for_unit);
	if(archive_for_unit=='i')
	{
		archive_for_i=-1;
	}
	archive_every=dectorateTimelength(archive_every, archive_every_unit);
	archive_for=dectorateTimelength(archive_for, archive_for_unit);
	
	var new_item=document.createElement('tr');
	new_item.id="archive_"+g.archive_item_id;
	new_item.innerHTML=tmpls.settings_archive_row.evaluate( { id: g.archive_item_id, archive_next: next_archival, archive_every_i: archive_every_i, archive_every: archive_every, archive_every_unit: archive_every_unit,
			archive_for_i: archive_for_i, archive_for: archive_for, archive_for_unit: archive_for_unit,
			archive_backup_type: archive_backup_type, archive_backup_type_str: backupTypeStr(archive_backup_type) } );
	
	var archive_table=getArchiveTable();
	
	if(archive_table.childNodes.length>2)
	{
		var n=0;
		for(var i=archive_table.childNodes.length-1;i>=0;--i)
		{
			if(archive_table.childNodes[i].nodeName.toLowerCase()=='tr')
				++n;
			
			if(n==2)
			{
				archive_table.insertBefore(new_item, archive_table.childNodes[i]);
				break;
			}
		}
	}
	
	++g.archive_item_id;
}
function replaceArchiveId(old_id, new_id)
{
	var item=I('archive_'+old_id);
	item.innerHTML=tmpls.settings_archive_row.evaluate( { id: new_id, archive_next: I('archive_next_'+old_id).value, archive_every_i: I('archive_every_'+old_id).value, archive_every: I('archive_every_str_'+old_id).innerHTML,
			archive_every_unit: I('archive_every_unit_'+old_id).value, archive_for_i: I('archive_for_'+old_id).value, archive_for: I('archive_for_str_'+old_id).innerHTML, archive_for_unit: I('archive_for_unit_'+old_id).value,
			archive_backup_type: I('archive_backup_type_'+old_id).value, archive_backup_type_str: backupTypeStr(I('archive_backup_type_'+old_id).value) } );
}
function deleteArchiveItem(id)
{
	var archive_table=getArchiveTable();
	g.archive_item_id=0;
	var rmobj;
	var old_id=0;
	for(var i=0;i<archive_table.childNodes.length;++i)
	{
		var obj=archive_table.childNodes[i];
		if(obj.nodeName.toLowerCase()=='tr')
		{
			if(obj.id=="archive_"+id)
			{
				rmobj=obj;
				++old_id;
			}
			else if(obj.id.indexOf("archive_")==0)
			{
				replaceArchiveId(old_id, g.archive_item_id);
				obj.id="archive_"+g.archive_item_id;
				++g.archive_item_id;
				++old_id;
			}
		}
	}
	archive_table.removeChild(rmobj);
}
function changeArchiveForUnit()
{
	if(I('archive_for_unit').value=='i')
	{
		I('archive_for').type="hidden";
	}
	else
	{
		I('archive_for').type="text";
	}
}
function startBackups()
{
	var cbs=document.getElementsByName("status_selected");
	var ids=[];
	for(var i=0;i<cbs.length;++i)
	{
		if(cbs[i].checked)
		{
			ids.push(cbs[i].value);
		}
	}
	if(ids.length>0)
	{	
		show_status1(g.status_detail, "", false, ids, false, I('backup_type').value);
	}
	else
	{
		alert(trans["no_client_selected"]);
	}
}
function stopBackup(clientid)
{
	if(!startLoading()) return;
	
	alert(trans["trying_to_stop_backup"]);
	g.progress_stop_id=clientid;
	show_progress1(clientid);
}