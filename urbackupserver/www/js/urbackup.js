g.main_nav_pos=5;
g.loading=false;
g.lang="-";
g.startup=true;
g.no_tab_mouse_click=false;
g.tabberidx=-1;
g.progress_stop_id=-1;
g.current_version=1005000000;
g.status_show_all=false;
g.ldap_login=false;
g.datatable_default_config={};
g.maximized=false;

g.languages=[ 
			    { l: "Deutsch", s: "de" },
				{ l: "English", s: "en" }, 
				{ l: "Español", s: "es" },
				{ l: "Français", s: "fr" },
				{ l: "Россия", s: "ru"},
				{ l: "简体中文", s: "zh_CN" },
				{ l: "繁体中文", s: "zh_TW" },
				{ l: "فارسی", s: "fa" },
				{ l: "українська мова", s: "uk" },
				{ l: "Português do Brasil", s: "pt_BR" },
				{ l: "slovenský jazyk", s: "sk"},
				{ l: "Nederlands", s: "nl" },
				{ l: "norsk", s: "no_NO" },
				{ l: "Italiano", s: "it_IT" },
				{ l: "České", s: "cs_CZ" },
				{ l: "Türk", s: "tr" },
				{ l: "Português", s: "pt" },
				{ l: "Svensk", s: "sv" },
				{ l: "Română", s: "ro" }
			];

g.languages.sort(function (a,b) { if(a.l>b.l) return 1; if(a.l<b.l) return -1; return 0; } );	

		
function init_datatables()
{		
	g.datatable_default_config = {
				"scrollX": true,
				"iDisplayLength" : 25,
				"sDom" : 'CT<"clear">lfrtip',
				"oColVis": {
					"bRestore": true,
					"sRestore": trans("Restore to default"),
					"buttonText": trans("Show/hide columns")
				},
				"oTableTools": {
					"aButtons": [
						{
							"sExtends":    "collection",
							"sButtonText": trans("Save"),
							"aButtons":    [ "csv", "xls", 
							{
								"sExtends": "pdf"
							}
							]
						}					
					],
					"sSwfPath": "copy_csv_xls_pdf.swf"
				},
				"sPaginationType": "full_numbers",
				//"sScrollX": "100%",
				"bScrollCollapse": true,
				"bStateSave": true,
				"iCookieDuration": 365*60*60*24,
				"oLanguage": {
					"oPaginate": {
						"sFirst": trans("First"),
						"sLast": trans("Last"),
						"sNext": trans("Next"),
						"sPrevious": trans("Previous")
					},
					"sEmptyTable": trans("No data available in table"),
					"sInfo": trans("Showing _START_ to _END_ of _TOTAL_ entries"),
					"sInfoEmpty": trans("Showing 0 to 0 of 0 entries"),
					"sInfoFiltered": trans("(filtered from _MAX_ total entries)"),
					"sInfoThousands": trans("sInfoThousands"),
					"sLengthMenu": trans("Show _MENU_ entries"),
					"sProcessing": trans("Processing..."),
					"sSearch": trans("Search:"),
					"sZeroRecords": trans("No matching records found")
				}
	};
}


function startup()
{
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=-1;
	g.session="";
	I('main_nav').innerHTML="";
	I('nav_pos').innerHTML="";
	
	if(!startLoading()) return;
	
	var available_langs="langs=";
	for(var i=0;i<g.languages.length;++i)
	{
		available_langs+=g.languages[i].s;
		if(i+1<g.languages.length)
			available_langs+=",";
	}
	
	new getJSON("login", available_langs, try_anonymous_login);
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


function change_lang_select()
{
	var selidx=I('change_lang_select').selectedIndex;
	
	var cookie_lang = g.languages[selidx].s;
	
	var d = new Date();
	d.setTime(d.getTime() + 365*24*60*60*1000);		
	document.cookie="urbackup_lang="+cookie_lang+"; expires="+d.toUTCString()+"";
	if(window.localStorage)
	{
		window.localStorage.setItem('urbackup_lang', cookie_lang);
	}
	
	change_lang(cookie_lang, true);
}


function change_lang(l, refresh)
{
	g.lang=l;
	
	var c="<select id=\"change_lang_select\" class=\"form-control input-sm\" onchange=\"change_lang_select()\">";
	for(var i=0;i<g.languages.length;++i)
	{
		if(g.languages[i].s==l)
		{
			c+="<option selected=\"selected\">"+g.languages[i].l+"</option>";
		}
		else
		{
			c+="<option>"+g.languages[i].l+"</option>";
		}
	}
	c+="</select>";
	
	I('languages').innerHTML=c;
	
	window.curr_trans=window.translations[l];

	if(refresh)
	{
		refresh_page();
	}
	
	init_datatables();
}


function try_anonymous_login(data)
{
	stopLoading();
	
	if(g.startup)
	{
		var lang="en";
		for(var i=0;i<g.languages.length;++i)
		{
			if(g.languages[i].s.toLowerCase()==data.lang.toLowerCase())
			{
				lang=g.languages[i].s;
				break;
			}
		}
		g.startup=false;
		
		cookie_lang = getCookie("urbackup_lang");
		
		if(!cookie_lang && window.localStorage)
		{
			cookie_lang = localStorage.getItem("urbackup_lang");
		}
		
		if(cookie_lang)
		{
			change_lang(cookie_lang, false);
		}
		else
		{		
			change_lang(lang, false);
		}
	}
	
	if(data.upgrading_database)
	{
		data.upgrade_error_text=trans("upgrade_error_text");
		var ndata=dustRender("upgrade_error", data);
		if(g.data_f!=ndata)
		{
			I('data_f').innerHTML=ndata;
			g.data_f=ndata;
		}
		return;
	}
	
	if(data.creating_filescache)
	{
		data.creating_filescache_text=trans("creating_filescache_text");
		var ndata=dustRender("file_cache_error", data);
		if(g.data_f!=ndata)
		{
			I('data_f').innerHTML=ndata;
			g.data_f=ndata;
		}
		return;
	}
	
	if(!g.maximized)
	{
		if(document.cookie.indexOf("bootstrap_maximize=true")!=-1
			|| (window.localStorage && localStorage.getItem("bootstrap_maximize")==="true") )
		{
			g.maximize_or_minimize();
		}
		else
		{
			I('maximize').innerHTML=trans('Maximize'); 
		}
	}
	
	if(trans("about") && I("about_link"))
	{
		I("about_link").innerHTML = trans("about");
	}
	
	var params;
	var hash = location.href.split('#').splice(1).join('#');
	if(hash.length>0)
	{
		params = deparam(hash);
	}
	if(params && params.token_data && params.clientname)
	{
		window.location.hash="";
		file_access(params);
	}
	else
	{		
		if(data.success)
		{
			g.session=data.session;
			build_main_nav();
			show_status1();
		}
		else
		{
			if(data.ldap_enabled)
			{
				g.ldap_enabled=true;
			}
			
			var ndata=dustRender("login");
			if(g.data_f!=ndata)
			{
				I('data_f').innerHTML=ndata;
				g.data_f=ndata;
			}
			
			if(data.admin_only)
			{
				I('username_row').style.display="none";
				I('username').value = data.admin_only;
				I("password").focus();
			}
			else
			{
				I('username').focus();
			}
		}
	}
}

function file_access(params)
{
	var p = "clientname="+encodeURIComponent(params.clientname);
	p+="&token_data="+encodeURIComponent(params["token_data"]);
	
	for(var i=0;;++i)
	{
		if(params["tokens"+i])
		{
			p+="&tokens"+i+"="+encodeURIComponent(params["tokens"+i]);
		}
		else
		{
			break;
		}
	}	
	
	if(params["path"])
	{
		p+="&path="+encodeURIComponent(base64_decode_dash(params["path"])).replace(/\//g,"%2F");
		p+="&is_file="+params["is_file"];
		p+="&sa=files";
	}

	if(!startLoading()) return;
	new getJSON("backups", p, show_backups2);
}

function startLoading()
{
	if(g.loading)
		return false;
	
	NProgress.start();
	g.loading=true;
	SetLoadingCursor();
	return true;
}

function stopLoading()
{
	NProgress.done();
	g.loading=false;
	ResetCursor();
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
				ndata=dustRender("main_nav_sel", {func: nav_items[i], name: trans("nav_item_"+(i+1)), params: p})+ndata;
			}
			else
			{
				ndata=dustRender("main_nav", {func: nav_items[i], name: trans("nav_item_"+(i+1)), params: p})+ndata;
			}
		}
	}
	I('main_nav').innerHTML=ndata;

	$('.nav li a').on('click',function(){
		$('.navbar-collapse.in').collapse('hide');
	})
}

function multiplyTimeSpan(ts, m)
{
	ts = unescapeHTML(ts);
	var timespans = ts.split(";");
	
	var ret = "";
	for(var i=0;i<timespans.length;++i)
	{
		if(ret.length!=0)
		{
			ret+=";"
		}
		var idx = timespans[i].indexOf("@");
		if(idx!=-1)
		{
			var d=parseFloat(timespans[i].substr(0, idx));
			ret+=d*m + timespans[i].substr(idx, timespans[i].length - idx);
		}
		else
		{
			d=parseFloat(timespans[i]);
			ret+=d*m;
		}
	}
	
	return ret;
}

function makeTimeSpanNegative(ts)
{
	ts = unescapeHTML(ts);
	var timespans = ts.split(";");
	
	var ret = "";
	for(var i=0;i<timespans.length;++i)
	{
		if(ret.length!=0)
		{
			ret+=";"
		}
		var idx = timespans[i].indexOf("@");
		if(idx!=-1)
		{
			var d=parseFloat(timespans[i].substr(0, idx));
			if(d>0)
				d*=-1;
				
			ret+=d + timespans[i].substr(idx, timespans[i].length - idx);
		}
		else
		{
			d=parseFloat(timespans[i]);
			if(d>0)
				d*=-1;
				
			ret+=d;
		}
	}
	
	return ret;
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
	if(p=="update_freq_incr"){ val=multiplyTimeSpan(val, 60.0*60.0); if(obj.disabled) val=makeTimeSpanNegative(val); }
	if(p=="update_freq_full" || p=="update_freq_image_full" || p=="update_freq_image_incr")
		{ val=multiplyTimeSpan(val, 60.0*60.0*24.0); if(obj.disabled) val=makeTimeSpanNegative(val); }
	if(p=="startup_backup_delay") val*=60;
	if(p=="local_speed") { if(val=="-" || val=="") val=-1; else val=multiplyTimeSpan(val, (1024*1024)/8); }
	if(p=="internet_speed") { if(val=="-" || val=="") val=-1; else val=multiplyTimeSpan(val, 1024/8); }
	if(p=="global_local_speed") { if(val=="-" || val=="") val=-1; else val=multiplyTimeSpan(val, (1024*1024)/8); }
	if(p=="global_internet_speed") { if(val=="-" || val=="") val=-1; else val=multiplyTimeSpan(val, 1024/8); }
	if(p=="update_stats_cachesize") val=Math.round(val*1024);
		
	return "&"+p+"="+encodeURIComponent(val+"");
}


function show_progress1(stop_backup)
{
	if(!stop_backup)
	{
		if(!startLoading()) return;
	}
	if(g.refresh_timeout!=-1)
	{
		clearTimeout(g.refresh_timeout);
	}
	g.refresh_timeout=0;
	show_progress11(true);
}


function show_progress11(manual_click)
{
	if(g.refresh_timeout==-1) return;
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=setTimeout(show_progress11, 10000);
	
	if(manual_click)
	{
		g.progress_cnt = 0;
	}
	else
	{
		g.progress_cnt += 1;
		
		if(g.progress_cnt>10)
		{
			g.progress_cnt = 0;
		}
	}	
	
	var pars="";
	if( g.progress_stop_id!=-1)
	{
		//alert(stop_clientid);
		pars="stop_clientid="+g.progress_stop_client_id+"&stop_id="+g.progress_stop_id;
		g.progress_stop_id=-1;
	}
	
	if(g.progress_cnt!=0)
	{
		if(pars.length>0) pars+="&";
		pars+="with_lastacts=0";
	}
	
	g.progress_first=true;
	new getJSON("progress", pars, manual_click ? show_progress2 : show_progress21);
	
	if(manual_click)
	{
		g.main_nav_pos=5;
		build_main_nav();
		I('nav_pos').innerHTML="";
	}
}

function show_progress21(data)
{
	if(I("show_progress") && !g.loading)
	{
		data.from_timeout=true;
		
		if(!data.lastacts)
		{
			data.lastacts = jQuery.extend(true, [], g.cached_lastacts);
		}
		
		show_progress2(data);
	}
}

function show_progress2(data)
{
	if(g.refresh_timeout==-1)
	{
		return;
	}
	if(g.main_nav_pos!=5) return;
	if(!I('show_progress') && !g.progress_first)
	{
		return;
	}
	
	g.progress_first=false;
	
	if(!data.from_timeout)
	{
		stopLoading();
	}
	
	var rows="";
	var tdata="";
	if(data.progress.length>0)
	{
		for(var i=0;i<data.progress.length;++i)
		{
			var action = data.progress[i].action;
			
			if(action==3 || action==4 || action==9)
			{
				data.progress[i].image=true;
			}
			
			if(action==8)
			{
				data.progress[i].file_restore=true;
			}
			
			if(action==10)
			{
				data.progress[i].client_update=true;
			}
			
			if(action==11 || action==12) //check database integrity, backup database
			{
				data.progress[i].show_details=true;
			}
			
			if(action==12) //backup database
			{
				data.progress[i].backups_interrupted=true;
			}
		
			data.progress[i].action=trans("action_"+data.progress[i].action);
			if(data.progress[i].pcdone>=0)
			{
				data.progress[i].percent=true;
			}
			else
			{
				data.progress[i].indexing=true;
			}
			
			if(action==14 || action==15 || action==11) //cleanup, check database integrity
			{
				data.progress[i].indexing=false;
			}
			
			if(data.progress[i].eta_ms>1000)
			{
				data.progress[i].eta = format_time_seconds(data.progress[i].eta_ms/1000);
			}
			else
			{
				data.progress[i].eta = "-";
			}
			
			if(data.progress[i].speed_bpms>1)
			{
				data.progress[i].speed = format_size_bits(data.progress[i].speed_bpms*8000) + "/s";
			}
			else
			{
				data.progress[i].speed = "-";
			}
			
			if(data.progress[i].total_bytes>=0)
			{
				data.progress[i].f_done_bytes = format_size(data.progress[i].done_bytes);
				data.progress[i].f_total_bytes = format_size(data.progress[i].total_bytes);
			}
			
			rows+=dustRender("progress_row", data.progress[i]);
		}
		tdata=dustRender("progress_table", {"rows": rows});
	}
	else
	{
		tdata=dustRender("progress_table_none");
	}
	
	if(data.lastacts && data.lastacts.length>0)
	{
		g.cached_lastacts = jQuery.extend(true, [], data.lastacts);
		
		rows="";
		for(var i=0;i<data.lastacts.length;++i)
		{
			var obj=data.lastacts[i];
			var action=0;
			var no_size=false;
			if(obj.restore!=0)
			{
				if(obj.image==0)
				{
					obj.file_restore=true;
					action=8;
				}
				else
				{
					action=9;
					obj.is_image=true;
				}
				no_size=true;
			}
			else if(obj.image==0)
			{
				if(obj.resumed==0)
				{
					if(obj.incremental>0)
						action=1;
					else
						action=2;
				}
				else
				{
					if(obj.incremental>0)
						action=5;
					else
						action=6;
				}
			}
			else
			{
				obj.is_image=true;
				
				if(obj.incremental>0)
					action=3;
				else
					action=4;				
			}
			var a="action_"+action;
			if(obj.del)
				a+="_d";
			obj.action=trans(a);
			if(no_size)
			{
				obj.size="-";
			}
			else
			{
				if(obj.size_bytes==-1)
					obj.size=trans("unknown");
				else
					obj.size=format_size(obj.size_bytes);
				if(obj.del)
					obj.size="-"+obj.size;
			}
			
			obj.duration/=60;
			obj.duration=Math.ceil(obj.duration);
			obj.duration+=" min";
			
			obj.backuptime = format_unix_timestamp(obj.backuptime);
						
			rows+=dustRender("lastacts_row", obj);
		}
		tdata+=dustRender("lastacts_table", {rows: rows});
	}
	
	tdata+="<span id='show_progress' style='display:none'></span>";
	
	if(g.data_f!=tdata)
	{
		I('data_f').innerHTML=tdata;
		g.data_f=tdata;
	}
	
	if(data.progress.length>0)
	{
		for(var i=0;i<data.progress.length;++i)
		{
			if(data.progress[i].past_speed_bpms.length>0)
			{
				data.progress[i].past_speed_bpms.push(data.progress[i].speed_bpms);
				$("#speed_sparkline_"+data.progress[i].id).sparkline(data.progress[i].past_speed_bpms,
					{disableTooltips: true});
			}
		}
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
	g.refresh_timeout=-1;
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

	if(I("statclient"))
	{
		$('#statclient').selectpicker('destroy');
	}
	
	var ndata="<div class=\"row\">";
	ndata+="<div class=\"col-sm-1\">";
	ndata+="<a class=\"btn btn-default\" href=\"javascript: show_statistics1()\">"+trans("overview")+"</a>";
	ndata+="</div>";
	if(data.users.length>0)
	{
		ndata+="<div class=\"col-sm-2\">";
		ndata+="<select onchange=\"stat_client()\" class=\"selectpicker\" data-live-search=\"true\" data-container=\"body\" id=\"statclient\">";
		if(g.settings_nav_pos<1)
		{
			ndata+="<option value=\"n\">"+trans("clients")+"</option>";
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
		ndata+="</select>";
		g.stat_data=data;
	}
	ndata+="</div>";
	ndata+="<br/>&nbsp<br/>";
	I('nav_pos').innerHTML=ndata;
	
	$('#statclient').selectpicker({});
}


function render_useagegraph_single(selectedIdx, idx, key, params)
{
	var ret="";
	if(selectedIdx==idx)
	{
		ret+="<strong>"+trans(key)+"</strong>";
	}
	else
	{
		ret+="<a href=\"javascript: createUsageGraph("+idx+", '"+params+"');\">"+trans(key)+"</a>";
	}
	return ret;
}
function render_sel_usagegraph(selectedIdx, params)
{
	var ret="<span style='float: right; z-Index: 100'>";
	ret+=render_useagegraph_single(selectedIdx, 0, "day", params);
	ret+=" | ";
	ret+=render_useagegraph_single(selectedIdx, 1, "month", params);
	ret+=" | ";
	ret+=render_useagegraph_single(selectedIdx, 2, "year", params);
	ret+="</span>";
	return ret;
}
function createUsageGraph(selectedIdx, params)
{
	var addUsagegraph = render_sel_usagegraph(selectedIdx, params);
	
	I('usagegraph').innerHTML = "<img src=\"images/indicator.gif\" />"+trans("loading")+"...";
	
	var scale;
	var dateFormat;
	var transKey;
	if(selectedIdx==1)
	{
		scale="m";
		dateFormat="%b";
		transKey="month";
	}
	else if(selectedIdx==2)
	{	
		scale="y";
		dateFormat="%y";
		transKey="year";
	}
	else
	{
		scale="d";
		dateFormat="%#d";
		transKey="day";
	}
	
	if(params.length>0)
	{
		params= "&" + params;
	}
	
	new loadGraph("usagegraph", "scale="+scale+params, "usagegraph", {pie: false, width: 640, height: 480, 
			title: trans("storage_usage_bar_graph_title"),
			colname1: trans(transKey),
			colname2: trans("storage_usage_bar_graph_colname2"),
			dateFormat: dateFormat },
		addUsagegraph);
}
function set_button_filename(buttons, text)
{
	for(var i=0;i<buttons.length;++i)
	{
		var cd = getISODatestamp();
		if(i==2)
		{
			cd+=".pdf";
		}
		else
		{
			cd+=".csv";
		}
		buttons[i].sFileName = text+" "+cd;
	}
	return buttons;
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
		/*obj.used=format_size(obj.used);
		obj.files=format_size(obj.files);
		obj.images=format_size(obj.images);*/
		rows+=dustRender("stat_general_row", obj);
	}
	ndata=dustRender("stat_general", {rows: rows, used_total: format_size(used_total), files_total: format_size(files_total),
				images_total: format_size(images_total), ses: g.session, maximized: g.maximized});
	if(g.data_f!=ndata)
	{	
		I('data_f').innerHTML=ndata;
		new loadGraph("piegraph", "", "piegraph", {pie: true, width: 640, height: 900, 
			title: trans("storage_usage_pie_graph_title"), colname1: trans("storage_usage_pie_graph_colname1"), colname2: trans("storage_usage_pie_graph_colname2") }, "" );
		
		createUsageGraph(0, "");
		
		var datatable_config = g.datatable_default_config;
		var sort_fun = function(idx)
		{
			var _idx = idx;			
			this.sort = function( source, type, val ) {
					if (type === 'set') {
						source["bsize"+idx] = val;
						source["bsize_display"+idx] = format_size(val)
						return;
					}
					else if (type === 'display') {
					  return source["bsize_display"+idx];
					}
					else if (type === 'filter') {
					  return source["bsize_display"+idx];
					}
					return source["bsize"+idx];
				};
		}
		
		datatable_config.aoColumnDefs =
			[ {
				"aTargets": [ 1 ],
				"mData": (new sort_fun(1)).sort
			},
			{
				"aTargets": [ 2 ],
				"mData": (new sort_fun(2)).sort
			},
			{
				"aTargets": [ 3 ],
				"mData": (new sort_fun(3)).sort
			}];
		
		datatable_config.aaSorting = [[ 3, "desc" ]];
		var save_buttons = datatable_config.oTableTools.aButtons[0].aButtons;
		save_buttons[2].mColumns = [0, 1, 2, 3];
		save_buttons[2].sTitle = "UrBackup statistics - " + getISODatestamp();
		save_buttons = set_button_filename(save_buttons, "UrBackup statistics");
		datatable_config.oTableTools.aButtons[0].aButtons = save_buttons;
		$("#statistics_table").dataTable(datatable_config);
		
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
		g.data_f=dustRender("stat_user", {clientid: id, clientname: name, ses: g.session});
		I('data_f').innerHTML=g.data_f;
		createUsageGraph(0, "clientid="+id);
		show_statistics2(g.stat_data);
	}
}

function show_status1(hostname, action, remove_client, stop_client_remove)
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=-1;
	var pars="";
	
	if(hostname && hostname.length>0)
	{
		if(pars!="")
		{
			pars+="&";
		}
		if(typeof action=="undefined" || action==0 || action==1)
		{
			pars+="hostname="+hostname;
			
			if(action==1)
			{
				pars+="&remove=true";
			}
		}
		else if(action==2)
		{
			pars+="clientname="+hostname;			
		}
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


function build_client_download_select(client_downloads)
{
	var ret="";
	for(var i=0;i<client_downloads.length;++i)
	{
		ret+="<option value=\""+client_downloads[i].id+"\">"+client_downloads[i].name+"</option>";
	}
	return ret;
}


function show_status2(data)
{
	stopLoading();
	if(g.main_nav_pos!=6) return;
	
	var ndata="";
	var rows="";
	var removed_clients=[];
	for(var i=0;i<data.status.length;++i)
	{
		var obj=data.status[i];
		if(obj.file_ok
			&& obj.last_filebackup_issues==0)
		{
			obj.file_style="success";
			obj.file_ok_t=trans("ok");
		}
		else if(obj.file_ok)
		{
			obj.file_style="warning";
			obj.file_ok_t=trans("completed_with_issues");
		}
		else
		{
			obj.file_style="danger";
			obj.file_ok_t=trans("no_recent_backup");
		}
		
		if(obj.os_simple
			&& obj.os_simple.length>0
			&& obj.os_simple!="windows")
		{
			obj.image_style="";
			obj.image_ok_t=trans("not_supported");
		}
		else
		{
			if(obj.image_ok)
			{
				obj.image_style="success";
				obj.image_ok_t=trans("ok");
			}
			else
			{
				obj.image_style="danger";
				obj.image_ok_t=trans("no_recent_backup");
			}
		}
		
		if(obj.lastbackup=="") obj.lastbackup=trans("backup_never");
		else obj.lastbackup = format_unix_timestamp(obj.lastbackup);
		
		if(obj.lastbackup_image=="") obj.lastbackup_image=trans("backup_never");
		else obj.lastbackup_image = format_unix_timestamp(obj.lastbackup_image);
		
		if(obj.online) obj.online=trans("yes");
		else obj.online=trans("no");
		
		if(obj.lastseen!="") obj.lastseen = format_unix_timestamp(obj.lastseen);
		
		obj.Action_remove_start="";
		obj.Action_remove_end="";
		
		if(data.remove_client)
		{
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
			obj.Actions_start="<!--";
			obj.Actions_end="-->";
		}
		
		obj.start_file_backup="";
		obj.start_image_backup="";
		
		if(obj.processes)
		{
			for(var j=0;j<obj.processes.length;++j)
			{
				var proc = obj.processes[j];
				if(proc.action>0 && proc.action<7)
				{
					if(proc.pcdone<0)
					{
						proc.indexing=true;
						proc.pcdone=0;
					}
					else
					{
						proc.percent=true;
					}
					
					if(proc.action==1 || proc.action==2 ||
						proc.action==5 || proc.action==6)
					{
						obj.start_file_backup+=dustRender("status_percent_done", proc);
					}
					else
					{
						obj.start_image_backup+=dustRender("status_percent_done", proc);
					}
				}
			}
		}
		
		obj.start_file_backup+="<span id=\"start_file_backup_"+obj.id+"\" style=\"display:none\" />";
		obj.start_image_backup+="<span id=\"start_image_backup_"+obj.id+"\" style=\"display:none\" />";
		
		if(obj.os_version_string)
		{
			obj.os_version_string = unescapeHTML(obj.os_version_string);
		}
		
		switch(obj.status)
		{
			case 0: obj.status="ok"; break;
			case 1: obj.status="incr_file"; break;
			case 2: obj.status="full_file"; break;
			case 3: obj.status="incr_image"; break;
			case 4: obj.status="full_image"; break;
			case 5: obj.status="resume_incr_file"; break;
			case 6: obj.status="resume_full_file"; break;
			case 10: obj.status=trans("starting"); break;
			case 11: obj.status=trans("ident_err")+" <a href=\"help.htm#ident_err\" target=\"_blank\">?</a>"; break;
			case 12: obj.status=trans("too_many_clients_err"); break;
			case 13: obj.status=trans("authentication_err"); break;
			default: obj.status="&nbsp;"
		}
		
		if(data.allow_modify_clients)
		{
			obj.show_select_box=true;
			
			if(!data.no_images)
			{
				obj.allow_image_backup_start=true;
			}
			if(!data.no_file_backups)
			{
				obj.allow_file_backup_start=true;
			}
		}
		
		if( obj.delete_pending && obj.delete_pending==1)
		{
			obj.remove_client=data.remove_client;
			removed_clients.push(obj);
		}
		else
		{
			if(!obj.rejected || g.status_show_all)
			{
				rows+=dustRender("status_detail_row", obj);
			}
		}
	}
	var dir_error="";
	if(data.dir_error)
	{
		var ext_text="";
		var generic_text=true;
		if(data.dir_error_ext) ext_text=" ("+data.dir_error_ext+")";
		if(data.system_err) ext_text+=". "+data.system_err;
		
		if(data.dir_error_hint)
		{
			if(data.dir_error_hint=="volume_not_accessible")
			{
				ext_text+="<br>The entire drive is not accessible. If this is a network drive be aware that network drives are per user and UrBackup server runs as \"Local System\" user per default so it wont see your network drive. In this case you should use the UNC notation instead (\\servername\share).";
			}
			else if(data.dir_error_hint=="folder_unc_access_denied")
			{
				ext_text+="<br>UrBackup is denied access to the network share, probably because it does not have the correct credentials to access the server. To setup UrBackup server to backup to a network share please see the FAQ: <a href=\"https://www.urbackup.org/faq.html#use_shares\">https://www.urbackup.org/faq.html#use_shares</a>";
			}
			else
			{
				ext_text+="<br>"+data.dir_error_hint;
			}
		}
		
		if( data.dir_error_ext
			&& data.dir_error_ext=="err_cannot_create_symbolic_links")
		{
			generic_text=false;
		}
		
		if(data.detail_err_str)
		{
			ext_text+="<br><br>Detailed error info:<br><pre>"+data.detail_err_str+"</pre>";
		}
		
		dir_error=dustRender("dir_error", {ext_text: ext_text, dir_error_text: trans("dir_error_text"), generic_text: generic_text});
	}
	
	var tmpdir_error="";
	if(data.tmpdir_error)
	{
		tmpdir_error=dustRender("tmpdir_error", {tmpdir_error_text: trans("tmpdir_error_text")});
	}
	
	var endian_info="";
	if(data.big_endian)
	{
		endian_info=dustRender("big_endian_info", {});
	}
	
	var nospc_stalled="";
	if(data.nospc_stalled)
	{
		nospc_stalled=dustRender("nospc_stalled", {nospc_stalled_text: trans("nospc_stalled_text")});
	}
	
	var database_error="";
	if(data.database_error)
	{
		database_error=dustRender("database_error", {database_error_text: trans("database_error_text")});
	}
	
	var nospc_fatal="";
	if(data.nospc_fatal)
	{
		nospc_fatal=dustRender("nospc_fatal", {nospc_fatal_text: trans("nospc_fatal_text")});
	}
	
	var dlt_mod_start="<!--";
	var dlt_mod_end="-->";
	if(data.allow_modify_clients)
	{
		dlt_mod_start="";
		dlt_mod_end="";
	}
	
	var extra_clients_rows="";
	
	if(data.extra_clients.length>0)
	{
		for(var i=0;i<data.extra_clients.length;++i)
		{
			var obj=data.extra_clients[i];
			
			if(obj.online) obj.online=trans("yes");
			else obj.online=trans("no");
			
			extra_clients_rows+=dustRender("status_detail_extra_row", obj);
		}
	}
	
	var status_can_show_all=false;
	var status_extra_clients=false;
	
	if(data.allow_extra_clients
		&& data.extra_clients.length>0)
	{
		if(!g.status_show_all)
		{
			status_can_show_all=true;
		}
		status_extra_clients=true;
	}
	
	var allow_add_client=false;
	if(data.allow_add_client)
	{
		allow_add_client=true;
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
		var no_images_start="";
		var no_images_stop="";
		if(data.no_images)
		{
			no_images_start="<!--";
			no_images_stop="-->";
		}
		var no_file_backups_start="";
		var no_file_backups_stop="";
		if(data.no_file_backups)
		{
			no_file_backups_start="<!--";
			no_file_backups_stop="-->";
		}
		var status_modify_params={rem_start: rem_start, rem_stop: rem_stop, backup_type_num: 0, no_images_start: no_images_start,
							      no_images_stop: no_images_stop, no_file_backups_start: no_file_backups_start, no_file_backups_stop: no_file_backups_stop };
								  
		modify_clients=dustRender("status_modify_clients", status_modify_params);
	}
	
	var show_select_box=false;
	if(data.allow_modify_clients)
	{
		show_select_box=true;
	}
	
	var status_client_download_windows="";
	var status_client_download_linux="";
	var status_client_download_mac="";
	var has_client_download=false;
	if(data.client_downloads)
	{
		var client_download_data=build_client_download_select(data.client_downloads);
		status_client_download_windows=dustRender("status_client_download", {download_clients: client_download_data, os: "windows", os_windows: true});
		status_client_download_linux=dustRender("status_client_download", {download_clients: client_download_data, os: "linux", os_linux: true});
		status_client_download_mac=dustRender("status_client_download", {download_clients: client_download_data, os: "mac", os_mac: true});
		has_client_download=true;
	}
	
	g.server_identity = data.server_identity;
	
	ndata=dustRender("status_detail", {rows: rows, ses: g.session, dir_error: dir_error, tmpdir_error: tmpdir_error,
		nospc_stalled: nospc_stalled, nospc_fatal: nospc_fatal, endian_info: endian_info,
		extra_clients_rows: extra_clients_rows, status_can_show_all: status_can_show_all, status_extra_clients: status_extra_clients,
		show_select_box: show_select_box,
		server_identity: data.server_identity, modify_clients: modify_clients,
		dlt_mod_start: dlt_mod_start, dlt_mod_end: dlt_mod_end, internet_client_added: data.added_new_client, new_authkey: data.new_authkey, new_clientname: data.new_clientname,
		status_client_download_windows: status_client_download_windows, status_client_download_linux: status_client_download_linux, status_client_download_mac: status_client_download_mac,
		database_error: database_error, removed_clients_table: removed_clients.length>0, removed_clients: removed_clients,
		has_client_download: has_client_download, allow_add_client:allow_add_client});
	
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
		
		if(data.no_images)
		{
			show_hide_column('status_table', 6, false);
			show_hide_column('status_table', 8, false);
		}
		
		if(data.no_file_backups)
		{
			show_hide_column('status_table', 5, false);
			show_hide_column('status_table', 7, false);
		}
		
		var datatable_config = g.datatable_default_config;
		
		datatable_config.aoColumnDefs = [
				{ "bVisible": false, "aTargets": [ 3, 9, 10, 11 ]
				}];
				
		if(data.allow_modify_clients)
		{
			datatable_config.aoColumnDefs.push({ "bSortable": false, 'aTargets': [ 0 ] });
		}
			
		datatable_config.oColVis.aiExclude = [ 1 ];
		
		if(data.allow_modify_clients)
		{
			datatable_config.oColVis.aiExclude.push(0);
			datatable_config.aoColumnDefs.push({ "bVisible": true, "aTargets": [0]});
		}
		
		if(data.no_images)
		{
			datatable_config.oColVis.aiExclude.push(6);
			datatable_config.oColVis.aiExclude.push(8);
		}
		
		if(data.no_file_backups)
		{
			datatable_config.oColVis.aiExclude.push(5);
			datatable_config.oColVis.aiExclude.push(7);
		}
		
		var columns = [ 1 ];
		
		if(!data.no_file_backups)
			columns.push(5);		
		if(!data.no_images)
			columns.push(6);
		if(!data.no_file_backups)
			columns.push(7);		
		if(!data.no_images)
			columns.push(8);
			
		

		var save_buttons = datatable_config.oTableTools.aButtons[0].aButtons;
		save_buttons[2].mColumns = columns;
		save_buttons[2].sTitle = "UrBackup client status - " + getISODatestamp();
		save_buttons = set_button_filename(save_buttons, "UrBackup client status");
		datatable_config.oTableTools.aButtons[0].aButtons = save_buttons;			
		
		datatable_config.oTableTools.aButtons[0].aButtons[2].mColumns = columns;
		
		datatable_config.drawCallback = function(settings) {
			$('select[id^="startbackup_"]').filter(
				function() {
					$(this).selectpicker();
					$(this).prop('selectedIndex', -1);
				});
		};
		datatable_config.preDrawCallback = function(settings) {
			$('select[id^="startbackup_"]').filter(
				function() {
					$( ".bootstrap-select.open" ).removeClass("open");
				});
		};
		
		$("#status_table").dataTable(datatable_config);
		
		$("#download_client_windows").selectpicker();
		$("#download_client_mac").selectpicker();
		$("#download_client_linux").selectpicker();
	}
	
	if(data.curr_version_num)
	{
		g.checkForNewVersion(data.curr_version_num, data.curr_version_str);
	}
	
	g.status_show_all=false;
}


g.checkForNewVersion = function(curr_version_num, curr_version_str)
{
	if(curr_version_num>g.current_version && I('new_version_available'))
	{
		I('new_version_available').innerHTML=dustRender("new_version_available", {new_version_number: curr_version_str} );
		I('new_version_available').style="visibility: visible";
	}
}

function downloadClientURL(clientid, authkey, os)
{
	var site_url = location.protocol+'//'+location.host+location.pathname;
	
	if(site_url.substr(site_url.length-1)!="/")
	{
		site_url+="/";
	}

	if(authkey)
	{
		authkey = "&authkey="+encodeURIComponent(authkey);
	}
	else
	{
		authkey = "";
	}
	var ses = g.session;
	g.session=null;
	var ret = site_url + getURL("download_client", "clientid="+clientid+authkey+"&os="+os);
	g.session = ses;
	return ret;
}

function downloadClient(clientid, authkey, os)
{
	var selidx=-1;
	
	if(os && I('download_client_'+os))
	{
		selidx = I('download_client_'+os).selectedIndex;
	}
	
	if(selidx!=-1 || clientid!=-1)
	{
		if(authkey)
		{
			authkey = "&authkey="+encodeURIComponent(authkey);
		}
		else
		{
			authkey = "";
		}
		
		if(clientid==-1)
		{
			clientid = I('download_client_'+os).value;
			$("#download_client_"+os).prop('selectedIndex', -1);
		}
		
		location.href=getURL("download_client", "clientid="+clientid+authkey+"&os="+os);
	}
}


function addExtraClient()
{
	if(I('hostname').value.length==0)
	{
		alert(trans("enter_hostname"));
		I('hostname').focus();
		return;
	}
	
	show_status1(I('hostname').value);
}


function addInternetClient()
{
	if(I('clientname').value.length==0)
	{
		alert(trans("enter_clientname"));
		I('clientname').focus();
		return;
	}
	
	show_status1(I('clientname').value, 2);
}


function removeExtraClient(id)
{
	show_status1(id+"", 1);
}


function show_backups1()
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=-1;
	new getJSON("backups", "", show_backups2);
	
	g.main_nav_pos=4;
	build_main_nav();
	I('nav_pos').innerHTML="";
}


function show_backups2(data)
{
	stopLoading();
	var ndata="";
	
	if(data.session && g.session.length==0)
	{
		g.session=data.session;
	}
	
	if(data.err)
	{
		if(data.err=="access_denied")
		{
			ndata=dustRender("backups_access_denied", data);
		}
		else
		{
			ndata=dustRender("backups_error", {err: data.err});
		}
	}
	else if(data.clients)
	{
		var rows="";
		for(var i=0;i<data.clients.length;++i)
		{
			var obj=data.clients[i];
			if(obj.lastbackup==0)
				obj.lastbackup="-";
			else
				obj.lastbackup = format_unix_timestamp(obj.lastbackup);
			rows+=dustRender("backups_clients_row", obj);
		}
		ndata=dustRender("backups_clients", {rows: rows, ses: g.session});
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
				obj.incr=trans("yes");
			else
				obj.incr=trans("no");
				
			var link_title="";
			var stopwatch_img="";
			
			if(obj.archive_timeout!=0)
			{
				link_title='title="'+trans("unarchived_in")+' '+format_time_seconds(obj.archive_timeout)+'"';
				stopwatch_img='<img src="images/stopwatch.png" />';
			}
			
			
			if(data.can_archive)
			{
				if( obj.archived>0 )
					obj.archived='<span '+link_title+'><a href="#" onclick="unarchive_single('+obj.id+', '+data.clientid+'); return false;">☑</a>'+stopwatch_img+'</span>';
				else
					obj.archived='<a href="#" onclick="archive_single('+obj.id+', '+data.clientid+'); return false;">☐</a>';
			}
			else
			{
				if( obj.archived>0 )
					obj.archived='<span '+link_title+'>☑'+stopwatch_img+'</span>';
				else
					obj.archived='☐';
			}
			
			obj.clientid=data.clientid;
			
			if(obj.backuptime!=0)
			{
				obj.backuptime = format_unix_timestamp(obj.backuptime);
			}
			else
			{
				obj.backuptime = "-";
			}
				
			rows+=dustRender("backups_backups_row", obj);
		}
		var show_client_breadcrumb=false;
		if(!data.token_authentication)
		{
			show_client_breadcrumb=true;
		}
		
		ndata=dustRender("backups_backups", {rows: rows, ses: g.session, clientname: data.clientname, clientid: data.clientid, show_client_breadcrumb: show_client_breadcrumb});
	}
	else if(data.files && !data.single_item)
	{
		var rows="";		
		var path=unescapeHTML(data.path);
		g.last_browse_backupid = data.backupid;
		g.last_browse_backuptime = data.backuptime;
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
			cp+="<a href=\"javascript: tabMouseClickBackups("+data.clientid+", "+data.backupid+")\">"+format_unix_timestamp(data.backuptime)+"</a> > ";
		}
		else
		{
			cp+="<strong>"+format_unix_timestamp(data.backuptime)+"</strong>"
		}
		
		for(var i=0;i<data.files.length;++i)
		{
			var obj=data.files[i];
			if(obj.dir)
			{
				obj.size="";
				obj.proc="Files";
			}
			else
			{
				obj.size=format_size(obj.size);				
				obj.proc="FilesDL";
			}
			
			if(obj.creat==0)
			{
				obj.creat="-";
			}
			else
			{
				obj.creat=format_unix_timestamp(obj.creat);
			}
			
			if(obj.mod==0)
			{
				obj.mod="-";
			}
			else
			{	
				obj.mod=format_unix_timestamp(obj.mod);
			}
			
			if(obj.access==0)
			{
				obj.access="-";
			}
			else
			{	
				obj.access=format_unix_timestamp(obj.access);
			}
			
			obj.clientid=data.clientid;
			obj.backupid=data.backupid;
			obj.path=encodeURIComponent(path+"/"+obj.name).replace(/'/g,"%27");
			obj.list_items=true;
			
			data.files[i]=obj;
		}
		
		for(var i=0;i<els.length;++i)
		{
			if(els[i].length>0)
			{
				curr_path+="/"+els[i];
				if(i+1<els.length)
				{
					cp+="<a href=\"#\" onClick=\"tabMouseClickFiles("+data.clientid+","+data.backupid+",'"+(curr_path==""?"/":encodeURIComponent(curr_path).replace(/'/g,"%27"))+"'); return false\">"+els[i]+"</a>";
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
		
		var server_confirms_restore="false";
		if(data.server_confirms_restore)
		{
			server_confirms_restore="true";
		}
		
		var folder_path = encodeURIComponent(path).replace(/'/g,"%27");
		var obj = {files: data.files, can_restore: data.can_restore, server_confirms_restore: server_confirms_restore,
			ses: g.session, clientname: data.clientname,
			clientid: data.clientid, cpath: cp, backuptime: format_unix_timestamp(data.backuptime),
			backupid: data.backupid, path: folder_path, folder_path: folder_path };
			
		if(!data.token_authentication)
		{
			obj.show_client_breadcrumb=true;
		}
			
		if( data.files.length>0 )
		{
			obj.download_zip=true;
		}
		
		ndata=dustRender("backups_files", obj);
	}
	else if(data.files && data.single_item)
	{
		if(data.path)
		{
			var path=unescapeHTML(data.path);
			
			var cp="";
			var folder_path="";

			var els=path.split("/");
			var curr_path="";
			
			var last_path="";
			for(var i=0;i<els.length-1;++i)
			{
				if(els[i].length>0)
				{
					last_path+="/"+els[i];
				}
			}
			
			if(g.last_browse_backupid)
			{
				if(els.length>1 && (els[1].length>0 || els.length>2))
				{	
					cp+="<a href=\"javascript: tabMouseClickBackups("+data.clientid+", "+g.last_browse_backupid+")\">"+format_unix_timestamp(g.last_browse_backuptime)+"</a> > ";
				}
				else
				{
					cp+="<strong>"+format_unix_timestamp(g.last_browse_backuptime)+"</strong>"
				}
			}
			
			for(var i=0;i<els.length;++i)
			{
				if(els[i].length>0)
				{
					curr_path+="/"+els[i];
					if(i+1<els.length)
					{
						folder_path+="/"+els[i];
						var proc = "tabMouseClickFiles";
						if(!g.last_browse_backupid)
						{
							proc = "tabMouseClickFilesAccess";
						}
						cp+="<a href=\"#\" onClick=\""+proc+"("+data.clientid+","+g.last_browse_backupid+",'"+(curr_path==""?"/":encodeURIComponent(curr_path).replace(/'/g,"%27"))+"'); return false\">"+els[i]+"</a>";
						
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
		}
		
		var items = [];
		
		var lastitemhash="-";
		var lastitemidx=-1;
		
		for(var i=0;i<data.files.length;++i)
		{
			var obj=data.files[i];
			if(obj.dir)
			{
				obj.size="&nbsp;";
				obj.proc="Files";
				obj.path=encodeURIComponent(path).replace(/'/g,"%27");
			}
			else
			{
				obj.size=format_size(obj.size);				
				obj.proc="FilesDL";
				obj.path=encodeURIComponent(path).replace(/'/g,"%27");
			}
			obj.clientid=data.clientid;
			
			if(obj.creat==0)
			{
				obj.creat="-";
			}
			else
			{
				obj.creat=format_unix_timestamp(obj.creat);
			}
			
			if(obj.mod==0)
			{
				obj.mod="-";
			}
			else
			{	
				obj.mod=format_unix_timestamp(obj.mod);
			}
			
			if(obj.access==0)
			{
				obj.access="-";
			}
			else
			{
				obj.access=format_unix_timestamp(obj.access);
			}
			
			obj.backuptime = format_unix_timestamp(obj.backuptime);
			
			obj.rowspan=1;
			obj.has_version=true;
			obj.version=1;
			
			if(obj.shahash && lastitemhash==obj.shahash)
			{
				items[lastitemidx].rowspan+=1;
				obj.has_version=false;
				obj.version=items[lastitemidx].version;
			}
			else if(obj.shahash)
			{
				lastitemidx=i;
				lastitemhash=obj.shahash;
			}
			
			if(obj.has_version)
			{
				for(var j=0;j<items.length;++j)
				{
					if(items[j].has_version)
					{
						items[j].version+=1;
					}
				}
			}
				
			items.push(obj);
		}
		
		var server_confirms_restore="false";
		if(data.server_confirms_restore)
		{
			server_confirms_restore="true";
		}
		
		var folder_path = encodeURIComponent(folder_path).replace(/'/g,"%27");
		var obj = {items: items, can_restore: data.can_restore, server_confirms_restore: server_confirms_restore,
			ses: g.session, clientname: data.clientname,
			clientid: data.clientid, cpath: cp,
			path: encodeURIComponent(path).replace(/'/g,"%27"), folder_path: folder_path };
			
		if(!data.token_authentication)
		{
			obj.show_client_breadcrumb=true;
		}
		
		ndata=dustRender("backup_item", obj);
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
				obj.childNodes[i].innerHTML="<img src=\"images/arr.png\" />";
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
function tabMouseClickFilesAccess(clientid, backupid, path)
{
	if(!startLoading()) return;
	new getJSON("backups", "sa=files&clientid="+clientid+"&path="+path.replace(/\//g,"%2F")+"&is_file=false", show_backups2);
}
function tabMouseClickFilesDLAccess(clientid, backupid, path)
{
	if(!startLoading()) return;
	new getJSON("backups", "sa=files&clientid="+clientid+"&path="+path.replace(/\//g,"%2F")+"&is_file=true", show_backups2);
}
function downloadZIP(clientid, backupid, path)
{
	location.href=getURL("backups", "sa=zipdl&clientid="+clientid+"&backupid="+backupid+"&path="+path.replace(/\//g,"%2F"));
}
function restoreFiles(clientid, backupid, path, server_confirms_restore, fn_filter)
{
	if(!startLoading()) return;
	
	if(server_confirms_restore)
	{
		if(!confirm("Are you sure you want to restore the selected files? Existing files will be overwritten. Files created within the selected folder since the backup will be deleted. When in doubt please cancel and run a file backup before proceeding."))
		{
			stopLoading();
			return;
		}
	}
	
	var filter="";
	if(fn_filter)
	{
		filter+="&filter="+encodeURIComponent(fn_filter.replace(/\//g,"%2F"))
	}
	
	g.curr_clientid = clientid;
	g.curr_backupid = backupid;
	g.curr_path = path;
	
	new getJSON("backups", "sa=clientdl&clientid="+clientid+"&backupid="+backupid+"&path="+path.replace(/\//g,"%2F")+filter, restore_callback);
}

function restore_callback(data)
{
	stopLoading();
	if(data.err)
	{
		alert("An error occured when starting restore: " + data.err);
	}
	else if(data.wait_key)
	{
		var ndata=dustRender("backup_restore_wait");
		if(g.data_f!=ndata)
		{
			I('data_f').innerHTML=ndata;
			g.data_f=ndata;
		}
		
		new getJSON("restore_prepare_wait", "wait_key="+data.wait_key, restore_prepare_wait_callback);
	}
}

function restore_prepare_wait_callback(data)
{
	if(!data.wait_key)
	{
		alert("An error occured while waiting for the restore process: "+data.err);
	}
	else
	{
		if(data.completed)
		{
			stopLoading();
			tabMouseClickFiles(g.curr_clientid, g.curr_backupid, g.curr_path);
		}
		else
		{
			new getJSON("restore_prepare_wait", "wait_key="+data.wait_key, restore_prepare_wait_callback);
		}
	}
}

function show_settings1()
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=-1;
	new getJSON("settings", "", show_settings2);
	
	g.main_nav_pos=1;
	g.settings_nav_pos=-1;
	build_main_nav();
	I('nav_pos').innerHTML="";
}
function show_settings2(data)
{
	var go_to_change_pw=false;
	stopLoading();
	if(data.navitems)
	{
		var n="";
		var nav=data.navitems;
		var idx=0;
		g.user_nav_pos_offset=0;
		g.mail_nav_pos_offset=0;
		g.ldap_nav_pos_offset=0;
		n+="<ul class=\"nav nav-tabs\" role=\"tablist\">";
		if(nav.general)
		{
			if(g.settings_nav_pos==-1) g.settings_nav_pos=0;
			
			if(g.settings_nav_pos==idx)
			{
				n+="<li class=\"active\"><a href=\"javascript: generalSettings()\">"+trans("general_settings")+"</a></li>";
			}
			else
			{
				n+="<li><a href=\"javascript: generalSettings()\">"+trans("general_settings")+"</a></li>";
			}
			
			++idx;
			++g.user_nav_pos_offset;
			++g.mail_nav_pos_offset;
			++g.ldap_nav_pos_offset;
			++g.internet_nav_pos_offset;
		}
		if(nav.mail)
		{
			if(g.settings_nav_pos==-1) g.settings_nav_pos=0;
			
			if(g.settings_nav_pos==idx)
			{
				n+="<li class=\"active\"><a href=\"javascript: mailSettings()\">"+trans("mail_settings")+"</a></li>";
			}
			else
			{
				n+="<li><a href=\"javascript: mailSettings()\">"+trans("mail_settings")+"</a></li>";
			}

			++idx;
			++g.user_nav_pos_offset;
			++g.ldap_nav_pos_offset;
			++g.internet_nav_pos_offset;
		}
		if(nav.ldap)
		{
			if(g.settings_nav_pos==-1) g.settings_nav_pos=0;
			
			if(g.settings_nav_pos==idx)
			{
				n+="<li class=\"active\"><a href=\"javascript: ldapSettings()\">"+trans("ldap_settings")+"</a></li>";
			}
			else
			{
				n+="<li><a href=\"javascript: ldapSettings()\">"+trans("ldap_settings")+"</a></li>";
			}

			++idx;
			++g.user_nav_pos_offset;
			++g.internet_nav_pos_offset;
		}
		if(nav.users)
		{
			if(g.settings_nav_pos==-1) g.settings_nav_pos=0;
			
			if(g.settings_nav_pos==idx)
			{
				n+="<li class=\"active\"><a href=\"javascript: userSettings()\">"+trans("users")+"</a></li>";
			}
			else
			{
				n+="<li><a href=\"javascript: userSettings()\">"+trans("users")+"</a></li>";
			}

			++idx;
			++g.user_nav_pos_offset;
		}
		else
		{
			if(g.settings_nav_pos==-1)
			{
				if(nav.clients && nav.clients.length>0)
				{
					g.settings_nav_pos=1;
				}
				else
				{
					g.settings_nav_pos=0;
					go_to_change_pw=true;
				}
			}
			
			if(g.settings_nav_pos==idx)
			{
				n+="<li id=\"change_pw_el\" class=\"active\"><a href=\"javascript: changePW(this)\">"+trans("change_pw")+"</a></li>";
			}
			else
			{
				n+="<li id=\"change_pw_el\"><a href=\"javascript: changePW(this)\">"+trans("change_pw")+"</a></li>";
			}

			++idx;
			++g.user_nav_pos_offset;
		}
		if(nav.clients)
		{
			if(g.settings_nav_pos==-1) g.settings_nav_pos=0;
			
			g.settings_clients=nav.clients;
			
			if(nav.clients.length>1)
			{
				n+="<select id=\"clientpicker\" class=\"selectpicker\" data-live-search=\"true\" data-container=\"body\" title=\""+trans("clients")+"\">";
				for(var i=0;i<nav.clients.length;++i)
				{		
					var selected = "";
					if(g.settings_nav_pos==idx)
					{
						selected = "selected=\"selected\"";
					}
					n+="<option value=\""+nav.clients[i].id + ";" + idx + "\" "+selected+">" + nav.clients[i].name + "</option>";
					++idx;
				}
				n+="</select>";
			}
			else if(nav.clients.length>0)
			{
				if(g.settings_nav_pos==idx)
				{
					n+="<li id=\"client_settings_el\" class=\"active\"><a href=\"javascript: clientSettings("+nav.clients[0].id+", "+idx+");\">"+trans("client_settings")+"</a></li>";
				}
				else
				{
					n+="<li id=\"client_settings_el\"><a href=\"javascript: clientSettings("+nav.clients[0].id+", "+idx+");\">"+trans("client_settings")+"</a></li>";
				}
				++idx;
			}
		}
		I('nav_pos').innerHTML=n;
		
		$("#clientpicker").change(function() {
			var vals = I("clientpicker")[I("clientpicker").selectedIndex].value.split(";");
			if(vals[0]=="n")
			{
				return;
			}
			clientSettings(vals[0], vals[1]);
		});
		
		$('#clientpicker').selectpicker({
		  style: 'btn',
		});
	}
	
	var ndata="";
	var tabber_set_idx=false;
	if(data.sa)
	{
		if(data.sa=="general")
		{
			data.settings.no_images=getCheckboxValue(data.settings.no_images);
			data.settings.no_file_backups=getCheckboxValue(data.settings.no_file_backups);
			data.settings.allow_overwrite=getCheckboxValue(data.settings.allow_overwrite);
			data.settings.autoshutdown=getCheckboxValue(data.settings.autoshutdown);
			data.settings.download_client=getCheckboxValue(data.settings.download_client);
			data.settings.autoupdate_clients=getCheckboxValue(data.settings.autoupdate_clients);
			data.settings.show_server_updates=getCheckboxValue(data.settings.show_server_updates);
			data.settings.backup_database=getCheckboxValue(data.settings.backup_database);
			data.settings.use_tmpfiles=getCheckboxValue(data.settings.use_tmpfiles);
			data.settings.use_tmpfiles_images=getCheckboxValue(data.settings.use_tmpfiles_images);
			data.settings.use_incremental_symlinks=getCheckboxValue(data.settings.use_incremental_symlinks);
			
			data.settings.allow_config_paths=getCheckboxValue(data.settings.allow_config_paths);
			data.settings.allow_starting_full_file_backups=getCheckboxValue(data.settings.allow_starting_full_file_backups);
			data.settings.allow_starting_incr_file_backups=getCheckboxValue(data.settings.allow_starting_incr_file_backups);
			data.settings.allow_starting_full_image_backups=getCheckboxValue(data.settings.allow_starting_full_image_backups);
			data.settings.allow_starting_incr_image_backups=getCheckboxValue(data.settings.allow_starting_incr_image_backups);
			data.settings.allow_pause=getCheckboxValue(data.settings.allow_pause);
			data.settings.allow_log_view=getCheckboxValue(data.settings.allow_log_view);
			data.settings.allow_tray_exit=getCheckboxValue(data.settings.allow_tray_exit);
			data.settings.allow_file_restore=getCheckboxValue(data.settings.allow_file_restore);
			data.settings.allow_component_restore=getCheckboxValue(data.settings.allow_component_restore);
			data.settings.allow_component_config=getCheckboxValue(data.settings.allow_component_config);
			
			data.settings.internet_full_file_backups=getCheckboxValue(data.settings.internet_full_file_backups);
			data.settings.internet_image_backups=getCheckboxValue(data.settings.internet_image_backups);
			data.settings.internet_mode_enabled=getCheckboxValue(data.settings.internet_mode_enabled);
			data.settings.internet_encrypt=getCheckboxValue(data.settings.internet_encrypt);
			data.settings.internet_compress=getCheckboxValue(data.settings.internet_compress);
			data.settings.silent_update=getCheckboxValue(data.settings.silent_update);
			data.settings.end_to_end_file_backup_verification=getCheckboxValue(data.settings.end_to_end_file_backup_verification);
			data.settings.internet_calculate_filehashes_on_client=getCheckboxValue(data.settings.internet_calculate_filehashes_on_client);
			data.settings.internet_connect_always=getCheckboxValue(data.settings.internet_connect_always);
			data.settings.verify_using_client_hashes=getCheckboxValue(data.settings.verify_using_client_hashes);
			data.settings.internet_readd_file_entries=getCheckboxValue(data.settings.internet_readd_file_entries);
			data.settings.background_backups=getCheckboxValue(data.settings.background_backups);
			data.settings.create_linked_user_views=getCheckboxValue(data.settings.create_linked_user_views);
			data.settings.ignore_disk_errors=getCheckboxValue(data.settings.ignore_disk_errors);
			
			var transfer_mode_params1=["raw", "hashed"];
			var transfer_mode_params2=["raw", "hashed", "blockhash"];
			
			data.settings=addSelectSelected(transfer_mode_params1, "local_full_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params1, "internet_full_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params2, "local_incr_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params2, "internet_incr_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params1, "local_image_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params1, "internet_image_transfer_mode", data.settings);
			
			var incr_image_style_params=["to-full", "to-last"];
			data.settings=addSelectSelected(incr_image_style_params, "local_incr_image_style", data.settings);
			data.settings=addSelectSelected(incr_image_style_params, "internet_incr_image_style", data.settings);
			
			var full_image_style_params=["full", "synthetic"];
			data.settings=addSelectSelected(full_image_style_params, "local_full_image_style", data.settings);
			data.settings=addSelectSelected(full_image_style_params, "internet_full_image_style", data.settings);
			
			var image_file_format_params = ["vhdz", "vhd"];
			if(data.cowraw_available)
			{
				data.settings.cowraw_available=true;
				image_file_format_params.push("cowraw");
			}
			data.settings=addSelectSelected(image_file_format_params, "image_file_format", data.settings);
			
			data.settings.update_freq_incr=multiplyTimeSpan(data.settings.update_freq_incr, 1/(60.0*60.0));
			data.settings.update_freq_full=multiplyTimeSpan(data.settings.update_freq_full, 1/(60.0*60.0*24.0));
			data.settings.update_freq_image_incr=multiplyTimeSpan(data.settings.update_freq_image_incr, 1/(60.0*60.0*24.0));
			data.settings.update_freq_image_full=multiplyTimeSpan(data.settings.update_freq_image_full, 1/(60.0*60.0*24.0));
			data.settings.startup_backup_delay/=60;
			
			if(data.settings.local_speed=="-1") data.settings.local_speed="-";
			else data.settings.local_speed=multiplyTimeSpan(data.settings.local_speed, 1/((1024*1024)/8));
			if(data.settings.internet_speed=="-1") data.settings.internet_speed="-";
			else data.settings.internet_speed=multiplyTimeSpan(data.settings.internet_speed, 1/(1024/8));
			
			if(data.settings.global_local_speed=="-1") data.settings.global_local_speed="-";
			else data.settings.global_local_speed=multiplyTimeSpan(data.settings.global_local_speed, 1/((1024*1024)/8));
			if(data.settings.global_internet_speed=="-1") data.settings.global_internet_speed="-";
			else data.settings.global_internet_speed=multiplyTimeSpan(data.settings.global_internet_speed, 1/(1024/8));
			
			data.settings.file_hash_collect_cachesize/=1024;
			data.settings.update_stats_cachesize/=1024;
			
			
			data.settings.no_compname_start="<!--";
			data.settings.no_compname_end="-->";
			
			data.settings.global_settings_start="";
			data.settings.global_settings_end="";
			
			data.settings.main_client = true;
			
			data.settings.client_plural="s";
			
			data.settings.ONLY_WIN32_BEGIN=unescapeHTML(data.settings.ONLY_WIN32_BEGIN);
			data.settings.ONLY_WIN32_END=unescapeHTML(data.settings.ONLY_WIN32_END);
			
			if(nav.internet)
			{
				data.settings.internet_settings_start="";
				data.settings.internet_settings_end="";
				data.settings.global_settings_start_inet=data.settings.global_settings_start;
				data.settings.global_settings_end_inet=data.settings.global_settings_end;
				data.settings.no_compname_start_inet=data.settings.no_compname_start;
				data.settings.no_compname_end_inet=data.settings.no_compname_end;
			}
			else
			{
				data.settings.internet_settings_start="<!--";
				data.settings.internet_settings_end="-->";
				data.settings.global_settings_start_inet="";
				data.settings.global_settings_end_inet="";
				data.settings.no_compname_start_inet="";
				data.settings.no_compname_end_inet="";
			}
			
			data.settings.client_settings=false;
			
			data.settings.settings_inv=dustRender("settings_inv_row", data.settings);
			ndata+=dustRender("settings_general", data.settings);
			
			if(data.saved_ok)
			{
				ndata+=dustRender("settings_save_ok");
				tabber_set_idx=true;
			}
		}
		else if(data.sa=="clientsettings")
		{
		
			if( data.settings.allow_overwrite==true
				&& data.settings.overwrite==true
				&& data.settings.client_set_settings==true )
			{
				data.settings.overwrite_warning_start="";
				data.settings.overwrite_warning_end="";
			}
			else
			{
				data.settings.overwrite_warning_start="<!--";
				data.settings.overwrite_warning_end="-->";
			}
			
			data.settings.overwrite=getCheckboxValue(data.settings.overwrite);
			data.settings.allow_overwrite=getCheckboxValue(data.settings.allow_overwrite);
			data.settings.allow_config_paths=getCheckboxValue(data.settings.allow_config_paths);
			data.settings.allow_starting_full_file_backups=getCheckboxValue(data.settings.allow_starting_full_file_backups);
			data.settings.allow_starting_incr_file_backups=getCheckboxValue(data.settings.allow_starting_incr_file_backups);
			data.settings.allow_starting_full_image_backups=getCheckboxValue(data.settings.allow_starting_full_image_backups);
			data.settings.allow_starting_incr_image_backups=getCheckboxValue(data.settings.allow_starting_incr_image_backups);
			data.settings.allow_pause=getCheckboxValue(data.settings.allow_pause);
			data.settings.allow_log_view=getCheckboxValue(data.settings.allow_log_view);
			data.settings.allow_tray_exit=getCheckboxValue(data.settings.allow_tray_exit);
			data.settings.allow_file_restore=getCheckboxValue(data.settings.allow_file_restore);
			data.settings.allow_component_restore=getCheckboxValue(data.settings.allow_component_restore);
			data.settings.allow_component_config=getCheckboxValue(data.settings.allow_component_config);
			
			data.settings.internet_mode_enabled=getCheckboxValue(data.settings.internet_mode_enabled);
			data.settings.internet_full_file_backups=getCheckboxValue(data.settings.internet_full_file_backups);
			data.settings.internet_image_backups=getCheckboxValue(data.settings.internet_image_backups);
			data.settings.internet_encrypt=getCheckboxValue(data.settings.internet_encrypt);
			data.settings.internet_compress=getCheckboxValue(data.settings.internet_compress);
			data.settings.silent_update=getCheckboxValue(data.settings.silent_update);
			data.settings.end_to_end_file_backup_verification=getCheckboxValue(data.settings.end_to_end_file_backup_verification);
			data.settings.internet_calculate_filehashes_on_client=getCheckboxValue(data.settings.internet_calculate_filehashes_on_client);
			data.settings.internet_connect_always=getCheckboxValue(data.settings.internet_connect_always);
			data.settings.verify_using_client_hashes=getCheckboxValue(data.settings.verify_using_client_hashes);
			data.settings.internet_readd_file_entries=getCheckboxValue(data.settings.internet_readd_file_entries);
			data.settings.background_backups=getCheckboxValue(data.settings.background_backups);
			data.settings.create_linked_user_views=getCheckboxValue(data.settings.create_linked_user_views);
			
			var transfer_mode_params1=["raw", "hashed"];
			var transfer_mode_params2=["raw", "hashed", "blockhash"];
			
			data.settings=addSelectSelected(transfer_mode_params1, "local_full_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params1, "internet_full_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params2, "local_incr_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params2, "internet_incr_file_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params1, "local_image_transfer_mode", data.settings);
			data.settings=addSelectSelected(transfer_mode_params1, "internet_image_transfer_mode", data.settings);
			
			var image_file_format_params = ["vhdz", "vhd"];
			if(data.cowraw_available)
			{
				data.settings.cowraw_available=true;
				image_file_format_params.push("cowraw");
			}
			data.settings=addSelectSelected(image_file_format_params, "image_file_format", data.settings);
			
			var incr_image_style_params=["to-full", "to-last"];
			data.settings=addSelectSelected(incr_image_style_params, "local_incr_image_style", data.settings);
			data.settings=addSelectSelected(incr_image_style_params, "internet_incr_image_style", data.settings);
			
			var full_image_style_params=["full", "synthetic"];
			data.settings=addSelectSelected(full_image_style_params, "local_full_image_style", data.settings);
			data.settings=addSelectSelected(full_image_style_params, "internet_full_image_style", data.settings);
			
			data.settings.update_freq_incr=multiplyTimeSpan(data.settings.update_freq_incr, 1/(60.0*60.0));
			data.settings.update_freq_full=multiplyTimeSpan(data.settings.update_freq_full, 1/(60.0*60.0*24.0));
			data.settings.update_freq_image_incr=multiplyTimeSpan(data.settings.update_freq_image_incr, 1/(60.0*60.0*24.0));
			data.settings.update_freq_image_full=multiplyTimeSpan(data.settings.update_freq_image_full, 1/(60.0*60.0*24.0));
			data.settings.startup_backup_delay/=60;
			
			if(data.settings.local_speed=="-1") data.settings.local_speed="-";
			else data.settings.local_speed=multiplyTimeSpan(data.settings.local_speed, 1/((1024*1024)/8));
			if(data.settings.internet_speed=="-1") data.settings.internet_speed="-";
			else data.settings.internet_speed=multiplyTimeSpan(data.settings.internet_speed, 1/(1024/8));
			
			data.settings.file_hash_collect_cachesize/=1024;
			
			data.settings.no_compname_start="";
			data.settings.no_compname_end="";
			data.settings.global_settings_start="<!--";
			data.settings.global_settings_end="-->";
			
			if(nav.internet)
			{
				data.settings.internet_settings_start="";
				data.settings.internet_settings_end="";
				data.settings.global_settings_start_inet=data.settings.global_settings_start;
				data.settings.global_settings_end_inet=data.settings.global_settings_end;
				data.settings.no_compname_start_inet=data.settings.no_compname_start;
				data.settings.no_compname_end_inet=data.settings.no_compname_end;
			}
			else
			{
				data.settings.internet_settings_start="<!--";
				data.settings.internet_settings_end="-->";
				data.settings.global_settings_start_inet="";
				data.settings.global_settings_end_inet="";
				data.settings.no_compname_start_inet="";
				data.settings.no_compname_end_inet="";
			}
			
			data.settings.client_settings=true;
						
			data.settings.settings_inv=dustRender("settings_inv_row", data.settings);
			ndata+=dustRender("settings_user", data.settings);
			
			if(data.saved_ok)
			{
				ndata+=dustRender("settings_save_ok");
				tabber_set_idx=true
			}
			else if(data.saved_part)
			{
				tabber_set_idx=true;
			}
		}
		else if(data.sa=="mail")
		{
			if(data.settings.mail_ssl_only=="true") data.settings.mail_ssl_only="checked=\"checked\"";
			else data.settings.mail_ssl_only="";
			if(data.settings.mail_check_certificate=="true") data.settings.mail_check_certificate="checked=\"checked\"";
			else data.settings.mail_check_certificate="";
			
			ndata+=dustRender("settings_mail", data.settings);
			
			if(data.saved_ok)
			{
				ndata+=dustRender("settings_save_ok");
			}
			if(data.mail_test)
			{
				if(data.mail_test=="ok")
				{
					ndata+=dustRender("settings_mail_test_ok");
				}
				else
				{
					ndata+=dustRender("settings_mail_test_failed", {mail_err: data.mail_test});
				}
			}
		}
		else if(data.sa=="ldap")
		{
			data.settings.ldap_login_enabled = getCheckboxValue(data.settings.ldap_login_enabled);
			
			if(data.ldap_test)
			{	
				data.settings.test_login=true;
				if(data.ldap_test=="ok")
				{
					data.settings.test_login_ok=true;
					data.settings.ldap_rights = data.ldap_rights;
				}
				else
				{
					data.settings.test_login_ok=false;
					data.settings.ldap_err = data.ldap_test;
				}				
			}
			else
			{
				data.settings.test_login=false;
			}
			
			ndata+=dustRender("settings_ldap", data.settings);
			
			if(data.saved_ok)
			{
				ndata+=dustRender("settings_save_ok");
			}
		}
		else if(data.sa=="listusers")
		{
			if(data.add_ok)
			{
				ndata+=dustRender("settings_user_add_done", {msg: trans("user_add_done") });
			}
			if(data.removeuser)
			{
				ndata+=dustRender("settings_user_add_done", {msg: trans("user_remove_done") });
			}
			if(data.update_right)
			{
				ndata+=dustRender("settings_user_add_done", {msg: trans("user_update_right_done") });
			}
			if(data.change_ok)
			{
				ndata+=dustRender("settings_user_add_done", {msg: trans("user_pw_change_ok") });
			}
			
			
			if(data.alread_exists)
			{
				alert(trans("user_exists"));
				return;
			}
		
			var rows="";
			if(data.users.length>0)
			{
				var num_users=0;
				var num_admins=0;
				for(var i=0;i<data.users.length;++i)
				{
					var obj=data.users[i];
					var is_admin=false;
					for(var j=0;j<obj.rights.length;++j)
					{
						var right=obj.rights[j];
						if(right.domain=="all" && right.right=="all")
						{
							is_admin=true;
						}
					}
					
					if(!is_admin)
					{
						num_users+=1;
					}
					else
					{
						num_admins+=1;
					}
				}
			
				g.user_rights={};
				for(var i=0;i<data.users.length;++i)
				{
					var obj=data.users[i];
					
					var t_rights=trans("user");
					
					g.user_rights[obj.id]=obj.rights;
					
					for(var j=0;j<obj.rights.length;++j)
					{
						var right=obj.rights[j];
						if(right.domain=="all" && right.right=="all")
						{
							t_rights=trans("admin");
						}
					}
					
					obj.rights=t_rights;
					
					if(t_rights==trans("admin") && num_users>0 && num_admins==1)
					{
						obj.can_change=false;
					}
					else
					{
						obj.can_change=true;
					}
					
					rows+=dustRender("settings_users_start_row", obj);
				}
			}
			else
			{
				rows=dustRender("settings_users_start_row_empty");
			}
			g.num_users=data.users.length;
			ndata+=dustRender("settings_users_start", { rows:rows });
		}
	}
	
	var update_tabber=false;
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
		update_tabber=true;
		if(I('backup_window_incr_file_row'))
		{
			hideBackupWindowDetails();
		}
	}
	
	settingsCheckboxChange();
	
	if(data.sa && data.sa=="clientsettings")
	{
		updateUserOverwrite();
	}
	else if(data.sa && data.sa=="change_pw_int")
	{
		changePW();
	}
	
	if(update_tabber && tabber_set_idx)
	{
		$('#settings_tabber a[href="' + g.active_pane_name + '"]').tab('show');
	}
	
	if(update_tabber && data.sa && (data.sa=="clientsettings" || data.sa=="general") )
	{
		g.archive_item_id=0;
		
		for(var i=0;i<data.archive_settings.length;++i)
		{
			var obj=data.archive_settings[i];
			addArchiveItemInt(getTimelengthUnit(obj.archive_every, obj.archive_every_unit), obj.archive_every_unit,
					getTimelengthUnit(obj.archive_for, obj.archive_for_unit), obj.archive_for_unit, obj.archive_backup_type, obj.next_archival, obj.archive_window, obj.archive_timeleft, data.sa=="general");
		}
	}
	
	$('#settings_tabber').bind('click', function (e) {
        g.active_pane_name = e.target.getAttribute("href");
    });
	
	if(go_to_change_pw)
	{
		changePW();
	}
}
function settingsCheckboxHandle(cbid)
{
	if(!I(cbid)) return;
	
	if(I(cbid+'_disable').checked && I(cbid).disabled==false)
	{
		I(cbid).disabled=true;
	}
	else if(!I(cbid+'_disable').checked && I(cbid).disabled==true)
	{
		I(cbid).disabled=false;
	}
	else if(!I(cbid+'_disable').checked && I(cbid).value<0)
	{
		I(cbid).value*=-1;
		I(cbid).disabled=true;
		I(cbid+'_disable').checked=true;
	}
}
function settingsCheckboxChange()
{
	settingsCheckboxHandle('update_freq_incr');
	settingsCheckboxHandle('update_freq_full');
	settingsCheckboxHandle('update_freq_image_incr');
	settingsCheckboxHandle('update_freq_image_full');
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
"backup_window_incr_file",
"backup_window_full_file",
"backup_window_incr_image",
"backup_window_full_image",
"computername",
"exclude_files",
"include_files",
"default_dirs",
"allow_config_paths",
"allow_starting_full_file_backups",
"allow_starting_incr_file_backups",
"allow_starting_full_image_backups",
"allow_starting_incr_image_backups",
"allow_pause",
"allow_log_view",
"allow_tray_exit",
"allow_file_restore",
"allow_component_restore",
"allow_component_config",
"image_letters",
"internet_authkey",
"internet_speed",
"local_speed",
"internet_image_backups",
"internet_full_file_backups",
"internet_encrypt",
"internet_compress",
"internet_mode_enabled",
"silent_update",
"client_quota",
"virtual_clients",
"end_to_end_file_backup_verification",
"local_full_file_transfer_mode",
"internet_full_file_transfer_mode",
"local_incr_file_transfer_mode",
"internet_incr_file_transfer_mode",
"local_image_transfer_mode",
"internet_image_transfer_mode",
"internet_calculate_filehashes_on_client",
"image_file_format",
"internet_connect_always",
"verify_using_client_hashes",
"internet_readd_file_entries",
"local_incr_image_style",
"local_full_image_style",
"background_backups",
"create_linked_user_views",
"internet_incr_image_style",
"internet_full_image_style",
"max_running_jobs_per_client",
"cbt_volumes",
"cbt_crash_persistent_volumes",
"ignore_disk_errors"
];
g.general_settings_list=[
"backupfolder",
"no_images",
"no_file_backups",
"autoshutdown",
"download_client",
"autoupdate_clients",
"max_sim_backups",
"max_active_clients",
"tmpdir",
"cleanup_window",
"backup_database",
"global_local_speed",
"global_internet_speed",
"use_tmpfiles",
"use_tmpfiles_images",
"update_stats_cachesize",
"global_soft_fs_quota",
"use_incremental_symlinks",
"show_server_updates",
"server_url"
];
g.mail_settings_list=[
"mail_servername",
"mail_serverport",
"mail_username",
"mail_password",
"mail_from",
"mail_ssl_only",
"mail_check_certificate",
"mail_admin_addrs"
];
g.internet_settings_list=[
"internet_server",
"internet_server_port"
];
g.ldap_settings_list=[
"ldap_login_enabled",
"ldap_server_name",
"ldap_server_port",
"ldap_username_prefix",
"ldap_username_suffix",
"ldap_group_class_query",
"ldap_group_key_name",
"ldap_class_key_name",
"ldap_group_rights_map",
"ldap_class_rights_map",
"testusername",
"testpassword"
];

g.time_span_regex = /^([\d.]*(@([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]\-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?\-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*)?[;]?)*$/i;

function validateCommonSettings()
{
	if(!validate_text_regex([{ id: "update_freq_incr", regexp: g.time_span_regex },
							 { id: "update_freq_full", regexp: g.time_span_regex },
							 { id: "update_freq_image_incr", regexp: g.time_span_regex },
							 { id: "update_freq_image_full", regexp: g.time_span_regex } ]) ) return false;
	if(!validate_text_int(["max_file_incr", "min_file_incr", "max_file_full", 
							"min_file_full", "max_image_incr", "min_image_incr", "max_image_full", "min_image_full",
							"startup_backup_delay"] ) ) return false;
	if(I('local_speed').value!="-" && !validate_text_regex({ id: "local_speed", regexp: g.time_span_regex})) return false;
	if(I('internet_speed') && !validate_text_regex({id: "internet_speed", regex: g.time_span_regex })) return false;
	var backup_window_regex = /^(([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]\-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?\-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*[;]?\s*)*$/i;
	if(!validate_text_regex([{ id: "backup_window_incr_file", errid: "backup_window", regexp: backup_window_regex },
							 { id: "backup_window_full_file", errid: "backup_window", regexp: backup_window_regex },
							 { id: "backup_window_incr_image", errid: "backup_window", regexp: backup_window_regex },
							 { id: "backup_window_full_image", errid: "backup_window", regexp: backup_window_regex } ]) ) return false;
	if(!validate_text_regex([{ id: "image_letters", regexp: /^(ALL)|(ALL_NONUSB)|(all)|(all_nonusb)|([A-Za-z][;,]?)*$/i }] ) ) return false;
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
		pars+=getPar("archive_window_"+i);
	}
	return pars;
}
function saveGeneralSettings()
{
	backupWindowChange();
	if(!validate_text_nonempty(["backupfolder"]) ) return;
	if(!validate_text_int(["max_sim_backups", "max_active_clients"]) ) return;
	if(I('global_local_speed').value!="-" && !validate_text_regex([{id: "global_local_speed", regexp: g.time_span_regex}])) return;
	if(I('global_internet_speed') && I('global_internet_speed').value!="-" && !validate_text_regex([{id: "global_internet_speed", regexp: g.time_span_regex}])) return;
	if(!validateCommonSettings() ) return;
	if(!validate_text_regex([{ id: "cleanup_window", regexp: /^(([mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]\-?[mon|mo|tu|tue|tues|di|wed|mi|th|thu|thur|thurs|do|fri|fr|sat|sa|sun|so|1-7]?\s*[,]?\s*)+\/([0-9][0-9]?:?[0-9]?[0-9]?\-[0-9][0-9]?:?[0-9]?[0-9]?\s*[,]?\s*)+\s*[;]?\s*)*$/i }]) ) return;	
	
	var internet_pars=getInternetSettings();
	if(internet_pars==null) return;
	
	if(!startLoading()) return;
			
	var pars="";
	for(var i=0;i<g.general_settings_list.length;++i)
	{
		pars+=getPar(g.general_settings_list[i]);
	}
	pars+=getArchivePars();
	for(var i=0;i<g.settings_list.length;++i)
	{
		pars+=getPar(g.settings_list[i]);
	}
	new getJSON("settings", "sa=general_save"+pars+internet_pars, show_settings2);
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
function saveLdapSettings()
{	
	if(!startLoading()) return;
	var pars="";
	for(var i=0;i<g.ldap_settings_list.length;++i)
	{
		pars+=getPar(g.ldap_settings_list[i]);
	}
	new getJSON("settings", "sa=ldap_save"+pars, show_settings2);
}
function getInternetSettings()
{	
	if(!I('internet_server_port')) return "";
	if(!validate_text_int(["internet_server_port"]) ) return null;
	var pars="";
	for(var i=0;i<g.internet_settings_list.length;++i)
	{
		pars+=getPar(g.internet_settings_list[i]);
	}
	return pars;
}
function clientSettings(clientid, idx)
{
	if(!startLoading()) return;
	g.settings_nav_pos=idx*1;
	new getJSON("settings", "sa=clientsettings&t_clientid="+clientid, show_settings2);
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
function ldapSettings()
{
	if(!startLoading()) return;
	g.settings_nav_pos=g.ldap_nav_pos_offset;
	new getJSON("settings", "sa=ldap", show_settings2);
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
		if( I(g.settings_list[i]) )
		{
			if(!I(g.settings_list[i]).disabled)
				I(g.settings_list[i]).disabled=!checked;
		}
	}
	
	I('user_submit').disabled=!checked;
	
	//Archive
	I('archive_add').disabled=!checked;
	I('archive_every').disabled=!checked;
	I('archive_for').disabled=!checked;
	I('archive_window').disabled=!checked;
	I('archive_every_unit').disabled=!checked;
	I('archive_for_unit').disabled=!checked;
	I('archive_backup_type').disabled=!checked;
	
	//Disable checkboxes
	if(!I('update_freq_incr_disable').disabled)
		I('update_freq_incr_disable').disabled=!checked;
		
	if(!I('update_freq_full_disable').disabled)
		I('update_freq_full_disable').disabled=!checked;
		
	if(!I('update_freq_image_incr_disable').disabled)
		I('update_freq_image_incr_disable').disabled=!checked;
		
	if(!I('update_freq_image_full_disable').disabled)
		I('update_freq_image_full_disable').disabled=!checked;
	
	I('backup_window').disabled=!checked;
	
	if(clientid)
	{
		saveClientSettings(clientid, true);
	}
}
function saveClientSettings(clientid, skip)
{
	if(!startLoading()) return;
	
	backupWindowChange();
	
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
		
	var rights="<select id=\"rights\" size=\"1\" style=\"width: 250px\" "+d+" class=\"selectpicker\" data-container=\"body\" data-live-search=\"true\">";
	rights+="<option value=\"-1\">"+trans("admin")+"</option>";
	
	for(var i=0;i<g.settings_clients.length;++i)
	{
		var obj=g.settings_clients[i];
		rights+="<option value=\""+obj.id+"\">"+obj.name+"</option>";
	}
	
	rights+="</select>";
	
	var ndata="";
	if(g.num_users==0)
		ndata=dustRender("settings_user_create_admin", { rights: rights });
	else
		ndata=dustRender("settings_user_create", { rights: rights });
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	
	$("#rights").selectpicker();
	
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
function createUser2()
{
	var username=I('username').value;
	var password1=I('password1').value;
	var password2=I('password2').value;
	
	if( username.length==0 )
	{	
		alert(trans("username_empty"));
		I('username').focus();
		return;
	}
	
	if( password1.length==0 )
	{
		alert(trans("password_empty"));
		I('password1').focus();
		return;
	}
	
	if( password1!=password2 )
	{
		alert(trans("password_differ"));
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
		t_rights=g.defaultUserRights(cid);
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
		alert(trans("username_empty"));
		I('username').focus();
		return false;
	}
	if( password.length==0 )
	{
		alert(trans("password_empty"));
		I('password').focus();
		return false;
	}
	
	if(!startLoading()) return false;
	
	if(!g.ldap_enabled)
	{
		new getJSON("salt", "username="+username, login2);
	}
	else
	{
		new getJSON("login", "username="+username+"&password="+password+"&plainpw=1", login3);
	}
	
	return false;
}
function login2(data)
{
	if(data.error==0)
	{
		alert(trans("user_n_exist"));
		stopLoading();
		I('username').focus();
		return;
	}
	
	if(data.ses)
		g.session=data.ses;
	
	var username=I('username').value;
	var password=I('password').value;
	
	var pwmd5 = calcMD5(data.salt+password);
	
	if(data.pbkdf2_rounds>0)
	{
		pwmd5 = sjcl.codec.hex.fromBits(sjcl.misc.pbkdf2(sjcl.codec.hex.toBits(pwmd5), data.salt, data.pbkdf2_rounds));
	}
	
	pwmd5 = calcMD5(data.rnd+pwmd5);
	
	new getJSON("login", "username="+username+"&password="+pwmd5, login3);
}
function login3(data)
{
	stopLoading();
	if(data.error==2)
	{
		alert(trans("password_wrong"));
		I('password').focus();
		return;
	}
	
	if(data.session)
		g.session=data.session;
	
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
	g.refresh_timeout=-1;
	stopLoading();
	alert(trans("session_timeout"));
	I('main_nav').innerHTML="";
	I('nav_pos').innerHTML="";
	g.session="";
	startup();
}
function deleteUser(uid)
{
	var c=confirm(trans("really_del_user"));
	if(c)
	{
		if(!startLoading()) return;
		new getJSON("settings", "sa=removeuser&userid="+uid, show_settings2);
	}
}
function changeUserPassword(uid, name)
{
	var ndata=dustRender("settings_user_pw_change", {userid: uid, username: name});
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	I('password1').focus();
}
function changePW(el)
{
	if(I('clientpicker'))
	{
		I('clientpicker').selectedIndex=-1;
	}
	$("#change_pw_el").addClass("active");
	$("#client_settings_el").removeClass("active");
	var ndata=dustRender("change_pw");
	g.settings_nav_pos=g.user_nav_pos_offset-1;
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	I('old_password').focus();
}
function doChangePW()
{	
	var password1=I('password1').value;
	var password2=I('password2').value;
	
	if( password1.length==0 )
	{
		alert(trans("password_empty"));
		I('password1').focus();
		return;
	}
	
	if( password1!=password2 )
	{
		alert(trans("password_differ"));
		I('password1').focus();
		return;
	}
	
	if(!startLoading()) return;
	new getJSON("salt", "", doChangePW2);
}
function doChangePW2(data)
{
	if(data.error==0)
	{
		alert(trans("user_n_exist"));
		stopLoading();
		I('old_password').focus();
		return;
	}
	
	var password=I('old_password').value;
	var password1=I('password1').value;
	
	var pwmd5 = calcMD5(data.salt+password);
	
	if(data.pbkdf2_rounds>0)
	{
		pwmd5 = sjcl.codec.hex.fromBits(sjcl.misc.pbkdf2(sjcl.codec.hex.toBits(pwmd5), data.salt, data.pbkdf2_rounds));
	}
	
	pwmd5=calcMD5(data.rnd+pwmd5);
	
	var new_salt = randomString();
	var pwmd5_new = calcMD5(new_salt + password1);
		
	var pars="&userid=own&pwmd5="+pwmd5_new+"&salt="+new_salt+"&old_pw="+pwmd5;
	
	new getJSON("settings", "sa=changepw"+pars, doChangePW3);
}

function doChangePW3(data)
{
	stopLoading();
	var ndata;
	if(data.change_ok)
	{
		ndata=dustRender("change_pw_ok");
	}
	else
	{
		var fail_reason="";
		if(data.old_pw_wrong)
		{
			alert(trans("old_pw_wrong"));
			I('old_password').focus();
			return;
		}
		ndata=dustRender("change_pw_fail", {fail_reason: fail_reason});
	}
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
}
function changeUserPW(uid)
{	
	var password1=I('password1').value;
	var password2=I('password2').value;
	
	if( password1.length==0 )
	{
		alert(trans("password_empty"));
		I('password1').focus();
		return;
	}
	
	if( password1!=password2 )
	{
		alert(trans("password_differ"));
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
				t=trans("right_all");
			}
			else if(right.value=="none")
			{
				t=trans("right_none");
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
		
		
		rows+=dustRender("settings_user_rights_change_row", obj);
	}
	var ndata=dustRender("settings_user_rights_change", {userid: uid, username: name, rows: rows});
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
	g.refresh_timeout=-1;
	
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
	g.refresh_timeout=-1;
	if(!params)params="";
	new getJSON("logs", params, show_logs2);
	
	g.main_nav_pos=3;
	build_main_nav();
	I('nav_pos').innerHTML="";
}

function show_logs2(data)
{
	stopLoading();
	
	var live_log_clients="";
	if(data.clients && !data.log)
	{
		var np="";
		np+="<form class=\"form-inline\" role=\"form\">";
		np+="<div class=\"form-group\">";
		np+="<label for=\"logsfilter\">";
		np+=trans("filter")+": ";
		np+="</label>";
		np+=dustRender("logs_filter");
		np+="<span style=\"margin-left:20px\">";
		np+="<select onchange=\"logClientChange()\" id=\"logclients\" class=\"selectpicker\" data-container=\"body\" data-live-search=\"true\">";
		np+="<option value=\"-1\">"+trans("all_clients")+"</option>";
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
		np+="</span>";
		np+="</div>";
		np+="</form>";
		
		
		I('nav_pos').innerHTML=np;
		$("#logclients").selectpicker();
		
		I('logsfilter').selectedIndex=2-data.ll;
		
		if(data.all_clients)
		{
			live_log_clients+="<option value=\"0\">"+trans("all_clients")+"</option>";
		}		
		for(var i=0;i<data.log_right_clients.length;++i)
		{
			var obj=data.log_right_clients[i];
			live_log_clients+="<option value=\""+obj.id+"\">"+obj.name+"</option>";
		}
	}
	else
	{
		var np=dustRender("log_single_filter");
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
				obj.eclass="danger";
			
			if(obj.warnings>0)
				obj.wclass="warning";
				
			var action=0;
			if(obj.restore!=0)
			{
				if(obj.image==0)
				{
					action=8;
				}
				else
				{
					action=9;
				}
			}
			else if(obj.image==0)
			{
				if(obj.resumed==0)
				{
					if(obj.incremental>0)
						action=1;
					else
						action=2;
				}
				else
				{
					if(obj.incremental>0)
						action=5;
					else
						action=6;
				}
			}
			else
			{
				if(obj.incremental>0)
					action=3;
				else
					action=4;
			}
			var a="action_"+action;
			
			obj.action=trans(a);
			
			obj.time = format_unix_timestamp(obj.time);
			
			rows+=dustRender("logs_row", obj);
		}
		if(data.logs.length==0)
			rows=dustRender("logs_none");
			
			
		var sel="selected=\"selected\"";
		
		var td={};
		td.rows=rows;
		td.report_mail=data.report_mail;
		td.sel_all=(data.report_sendonly==0)?sel:"";
		td.sel_failed=(data.report_sendonly==1)?sel:"";
		td.sel_succ=(data.report_sendonly==2)?sel:"";
		td.sel_failed_clienttimeout=(data.report_sendonly==3)?sel:"";
		td.sel_info=(data.report_loglevel==0)?sel:"";
		td.sel_warn=(data.report_loglevel==1)?sel:"";
		td.sel_error=(data.report_loglevel==2)?sel:"";
		td.live_log_clients=live_log_clients;
		if(data.has_user)
		{
			td.has_user=true;
		}
			
		ndata+=dustRender("logs_table", td);
	}
	
	if(data.log)
	{
		g.logdata=data.log.data;
		var ll=2;
		if(g.has_logfilter)
			ll=g.logfilter;
		var rows=createLog(g.logdata,ll);
		if(rows=="")
			rows=dustRender("log_single_none");
		g.logclientname=data.log.clientname;
		var params="";
		if(g.has_logsfilter)
			params+="ll="+g.logsfilter;
		if(g.has_logclients && g.logclients!=-1)
		{
			if(params.length>0) params+="&";
			params+="filter="+g.logclients;
		}
		ndata+=dustRender("log_single", {rows:rows, name: data.log.clientname, params: params});
	}
	
	if(data.saved_ok)
	{
		ndata+=dustRender("settings_save_ok");
	}	
	
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	
	$("#live_log_clientid").selectpicker();
	$("#live_log_clientid").prop('selectedIndex', -1);
	
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
	if(!I('report_mail'))
	{
		return;
	}
	var d="";
	var a=I('report_mail').value.split(';');
	for(var i=0;i<a.length;++i)
	{
		if(a[i]!="")
		{
			d+=dustRender("logs_report_mail",  {report_single_mail: a[i], num: i} );
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
					obj.time=format_unix_timestamp(obj.time);
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
				
			obj.level=trans("loglevel_"+obj.level);
			
			rows+=dustRender("log_single_row", obj);
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
			rows=dustRender("log_single_none");
	var ndata=dustRender("log_single", {rows:rows, name: g.logclientname});
	
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
	g.has_logfilter=true;
	g.logfilter=v;
	
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
function show_live_log()
{
	var clientid=I('live_log_clientid').value;
	var win = window.open('', '_blank', '');
	var live_log_page = dustRender("live_log", {session: g.session, clientid: clientid, clientname: I('live_log_clientid').options[I('live_log_clientid').selectedIndex].text});
	I('live_log_clientid').selectedIndex=-1;
	win.document.write(live_log_page);
	win.document.close();
	win.focus();
}
function showBackupLiveLog(clientid, clientname, logId)
{
	var win = window.open('', '_blank', '');
	var live_log_page = dustRender("live_log", {session: g.session, clientid: clientid,
												clientname: clientname,
												logid: logId});
	win.document.write(live_log_page);
	win.document.close();
	win.focus();
}
function removeClient(clientid)
{
	var b=confirm(trans("really_remove_client"));
	if(b)
	{
		show_status1("", false, [clientid]);
	}
}
function removeClients(clientid)
{
	if(clientid)
	{
		removeClient(clientid);
		return;
	}
	
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
		removeClient(ids[0]);
		return;
	}
	else if(ids.length>0)
	{	
		var b=confirm(trans("really_remove_clients"));
		if(b)
		{
			show_status1("", false, ids);
		}
	}
	else
	{
		alert(trans("no_client_selected"));
	}
}
function selectClientsToggle()
{
	if(I("status_selected_toggle").checked)
	{
		selectAllClients();
	}
	else
	{
		selectNoClients();
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
	show_status1("", false, [clientid], true);
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
function addArchiveItem(global)
{
	if(!validate_text_nonempty(["archive_every"])) return;
	if(I('archive_for_unit').value!="i")
	{
		if(!validate_text_nonempty(["archive_for"])) return;
	}
	if(!validate_text_regex([{id: "archive_window", regexp: /^((([0-9]+,?)+)|\*);((([0-9]+,?)+)|\*);((([0-9]+,?)+)|\*);((([0-9]+,?)+)|\*)$/i } ]) ) return;
	addArchiveItemInt(parseInt(I('archive_every').value), I('archive_every_unit').value, parseInt(I('archive_for').value), I('archive_for_unit').value, I('archive_backup_type').value, -1, I('archive_window').value, (global?"-":-1), global);
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
	if(unit=='h') tl+=" "+trans(addPlural(tl, "hour"));
	else if(unit=='d') tl+=" "+trans(addPlural(tl,"day"));
	else if(unit=='w') tl+=" "+trans(addPlural(tl,"week"));
	else if(unit=='m') tl+=" "+trans(addPlural(tl,"month"));
	else if(unit=='y') tl+=" "+trans(addPlural(tl,"year"));
	else if(unit=='i') tl=trans("forever");
	return  tl;
}
function backupTypeStr(bt)
{
	if(bt=="incr_file") return trans("action_1");
	else if(bt=="full_file") return trans("action_2");
	else if(bt=="file") return trans("file_backup");
}
function getArchiveTable()
{
	archive_table=I('archive_table').childNodes[1];
	return archive_table;
}
function addArchiveItemInt(archive_every, archive_every_unit, archive_for, archive_for_unit, archive_backup_type, next_archival, archive_window, archive_timeleft, global)
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
	
	var row_vals={ id: g.archive_item_id, archive_next: next_archival, archive_every_i: archive_every_i, archive_every: archive_every, archive_every_unit: archive_every_unit,
			archive_for_i: archive_for_i, archive_for: archive_for, archive_for_unit: archive_for_unit,
			archive_backup_type: archive_backup_type, archive_backup_type_str: backupTypeStr(archive_backup_type), archive_window: archive_window,
			show_archive_timeleft: !global};
	
	if(archive_timeleft!="-")
	{
		if(archive_timeleft<=0)
		{
			archive_timeleft=trans("wait_for_archive_window");
		}
		else
		{
			archive_timeleft=format_time_seconds(archive_timeleft, true);
		}
		
		row_vals.archive_timeleft=archive_timeleft;
	}
	
	new_item.innerHTML=dustRender("settings_archive_row",  row_vals );
	
	var archive_table=getArchiveTable();
	
	archive_table.appendChild(new_item);
	
	++g.archive_item_id;
}
function replaceArchiveId(old_id, new_id)
{
	var archive_timeleft;
	var show_archive_timeleft=false;
	if(I('archive_timeleft_'+old_id))
	{
		archive_timeleft = I('archive_timeleft_'+old_id).value;
		show_archive_timeleft=true;
	}
	var item=I('archive_'+old_id);
	item.innerHTML=dustRender("settings_archive_row",  { id: new_id, archive_next: I('archive_next_'+old_id).value, archive_every_i: I('archive_every_'+old_id).value, archive_every: I('archive_every_str_'+old_id).innerHTML,
			archive_every_unit: I('archive_every_unit_'+old_id).value, archive_for_i: I('archive_for_'+old_id).value, archive_for: I('archive_for_str_'+old_id).innerHTML, archive_for_unit: I('archive_for_unit_'+old_id).value,
			archive_backup_type: I('archive_backup_type_'+old_id).value, archive_backup_type_str: backupTypeStr(I('archive_backup_type_'+old_id).value), archive_window: I('archive_window_'+old_id).value,
			archive_timeleft: archive_timeleft, show_archive_timeleft: show_archive_timeleft } );
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
function startBackups(start_type, clientid)
{
	$(this).prop('selectedIndex', -1);
	
	if(start_type=="none")
	{
		return;
	}
	
	if(start_type=="remove")
	{
		removeClients(clientid);
		return;
	}
	
	var ids=[];
	if(clientid)
	{
		ids.push(clientid);
	}
	else
	{
		var cbs=document.getElementsByName("status_selected");
		for(var i=0;i<cbs.length;++i)
		{
			if(cbs[i].checked)
			{
				ids.push(cbs[i].value);
			}
		}
	}
	if(ids.length>0)
	{	
		startLoading();
		new getJSON("start_backup", "start_type="+start_type+"&start_client="+ids.join(","), backups_started);
	}
	else
	{
		alert(trans("no_client_selected"));
	}
}
function backups_started(data)
{
	stopLoading();
	
	if(data.result)
	{
		for(var i=0;i<data.result.length;++i)
		{
			var res = data.result[i];
			
			var text;
			var dom_id;
			if(res.start_type==="full_file" || res.start_type==="incr_file")
			{
				dom_id = 'start_file_backup_'+res.clientid;
				if(res.start_ok)
				{
					text = "<br />"+trans("queued_backup");
				}
				else
				{
					text = "<br />"+trans("starting_backup_failed");
				}
			}
			else
			{
				dom_id = 'start_image_backup_'+res.clientid;
				if(res.start_ok)
				{
					text = "<br />"+trans("queued_backup");
				}
				else
				{
					text = "<br />"+trans("starting_backup_failed");
				}
			}
			
			I(dom_id).style.display="inline";
			I(dom_id).innerHTML = text;
		}
	}
}
function stopBackup(clientid, id)
{
	if(!startLoading()) return;
	
	alert(trans("trying_to_stop_backup"));
	g.progress_stop_client_id=clientid;
	g.progress_stop_id=id;
	show_progress1(true);
}
function recalculateStatistics()
{
	if(!startLoading()) return;
	clearTimeout(g.refresh_timeout);
	g.refresh_timeout=-1;
	
	if(confirm(trans("really_recalculate")))
	{
		new getJSON("usage", "recalculate=true", show_statistics3);
	}
	else
	{
		stopLoading();
	}
}

g.showing_backup_window_details=true;

function setBackupWindowDisplay(display)
{
	I('backup_window_incr_file_row').style.display=display;
	I('backup_window_full_file_row').style.display=display;
	I('backup_window_incr_image_row').style.display=display;
	I('backup_window_full_image_row').style.display=display;
}
function showBackupWindowDetails()
{
	setBackupWindowDisplay("inline");
	I('backup_window_row').style.display="none";
	g.showing_backup_window_details=true;
}
function hideBackupWindowDetails()
{
	if(I('backup_window_incr_file').value==I('backup_window_full_file').value
	    && I('backup_window_full_file').value==I('backup_window_incr_image').value
	    && I('backup_window_incr_image').value==I('backup_window_full_image').value )
	{
		setBackupWindowDisplay("none");
		I('backup_window').value=I('backup_window_incr_file').value;
		g.showing_backup_window_details=false;
	}
	else
	{
		showBackupWindowDetails();
	}
}

function backupWindowChange()
{
	if(!g.showing_backup_window_details)
	{
		var val = I('backup_window').value;
		I('backup_window_incr_file').value=val;
		I('backup_window_full_file').value=val;
		I('backup_window_incr_image').value=val;
		I('backup_window_full_image').value=val;
	}
}

g.maximize_or_minimize = function(refresh)
{
	if(I('boostrap_container').className==="container")
	{
		I('boostrap_container').className='container-fluid';
		I('maximize').innerHTML=trans('Minimize');
		var d = new Date();
		d.setTime(d.getTime() + 365*24*60*60*1000);		
		document.cookie="bootstrap_maximize=true; expires="+d.toUTCString()+"";
		if(window.localStorage)
		{
			window.localStorage.setItem('bootstrap_maximize', 'true');
		}
		g.maximized=true;
	}
	else
	{
		I('boostrap_container').className='container';
		I('maximize').innerHTML=trans('Maximize'); 
		document.cookie="bootstrap_maximize=";
		if(window.localStorage)
		{
			window.localStorage.setItem('bootstrap_maximize', 'false');
		}
		g.maximized=false;
	}
	
	if($("#status_table"))
	{
		$("#status_table").DataTable().columns.adjust().draw();
	}
	
	if($("#statistics_table"))
	{
		$("#statistics_table").DataTable().columns.adjust().draw();
	}
	
	if(refresh)
	{
		refresh_page();
	}
}

function addNewClient1()
{
	if(!startLoading()) return;
	stopLoading();
	
	var ndata=dustRender("add_client", {server_identity: g.server_identity});
	
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
}

function addNewClient2()
{	
	if(I("choice_internet_client").checked)
	{
		if(!validate_text_nonempty(['internet_client_name']) )
		{
			return;
		}
		
		if(!startLoading()) return;
		new getJSON("add_client", "clientname="+encodeURIComponent(I("internet_client_name").value), addNewClient3);
	}
	else
	{
		addExtraClient();
	}
}

function addNewClient3(data)
{
	stopLoading();
	
	if(data.already_exists)
	{
		alert(trans("client_exists"));
	}
	else
	{
		data.linux_url = downloadClientURL(data.new_clientid, data.new_authkey, "linux");
		data.mac_url = downloadClientURL(data.new_clientid, data.new_authkey, "mac");
		var ndata=dustRender("client_added", data);
	
		if(g.data_f!=ndata)
		{
			I('data_f').innerHTML=ndata;
			g.data_f=ndata;
		}
	}
}
function aboutUrBackup()
{
	if(!startLoading()) return;
	stopLoading();
	
	var ndata=dustRender("about_urbackup", {version: I('server_version_full').innerHTML.trim()});
	if(g.data_f!=ndata)
	{
		I('data_f').innerHTML=ndata;
		g.data_f=ndata;
	}
	I('nav_pos').innerHTML="";
}