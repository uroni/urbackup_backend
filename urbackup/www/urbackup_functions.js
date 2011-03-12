var onloads = new Array();
if(!window.g)
	g=new Object();
	
g.debug=true;
	
g.opera=false;
if( navigator.userAgent.indexOf("Opera")!=-1 )
{
	g.opera=true;
}
g.ie=false;
if( navigator.appName=="Microsoft Internet Explorer" && g.opera==false )
{
	g.ie=true;
}

function Escape(str)
{
	return encodeURIComponent(str);
}
	
function iernd()
{
	if(g.ie==true && window.Ajax )
	{
		var rnd=Math.floor(Math.random()*100000);
		return '&iernd='+rnd;
	}
	return '';
}

function rnd()
{
	return Math.floor(Math.random()*100000);
}

function I(a)
{
	if( window.Ajax )
		return $(a);
	else
		return document.getElementById(a);
}		
	
function bodyOnLoad() {
	for ( var i = 0 ; i < onloads.length ; i++ )
	    onloads[i]();
}
	
function getStyle( Obj )
{
	try
	{
		return getComputedStyle(Obj, null )
	}
	catch( IE )
	{
		return Obj.currentStyle;
	}
}
	
function getInt( Str )
{
	return parseInt(Str.slice(0,-2) );
}
	
function getFloat( Str )
{
	return parseFloat(Str.slice(0,-2) );
}
	
function getScreenX()
{
	if( window.innerWidth )
		return window.innerWidth;
	else if( document.body.clientWidth )
		return document.body.clientWidth;
	else
		return document.documentElement.clientWidth;
}
	
function getScreenY()
{
	if( window.innerHeight )
		return window.innerHeight;
	else if( document.body.clientHeight )
		return document.body.clientHeight;
	else
		return document.documentElement.clientHeight;
}
		
function SetLoadingCursor()
{
	document.getElementsByTagName('body')[0].style.cursor="wait";
}
	
function ResetCursor()
{
	document.getElementsByTagName('body')[0].style.cursor="auto";
	setTimeout("ResetCursor()",100);
}
	
function StartLoad()
{
	I('loading_sign').style.visibility="visible";
	SetLoadingCursor();
}
	
function StopLoad()
{
	I('loading_sign').style.visibility="hidden";		
	ResetCursor();
}

function getURL(action, parameters)
{
	var ses="";
	if(g.session!=null && g.session!="" )
	{
		ses="&ses="+g.session;
	}
	if(g.lang)
	{
		ses+="&lang="+g.lang;
	}
	if(parameters!="" && parameters!=null )
	{
		return "x?a="+action+ses+"&"+parameters+iernd();
	}
	else
	{
		
		return "x?a="+action+ses+iernd();
	}
}
	
function AJAXRequestPure(action, parameters)
{
	if( window.Ajax )
		new window.Ajax.Request(getURL(action, parameters), { method: 'get' });
	else
	{
		req = new window.qx.io.remote.Request(getURL(action, parameters), "GET", "text/javascript");
		req.send();
	}
}

function AJAXUpdate(container, action, parameters)
{
	new window.Ajax.Updater( container, getURL( action, parameters), { evalScripts: 'true', method: 'get' } );
}

function clone(obj){
    if(obj == null || typeof(obj) != 'object')
        return obj;

    var temp = obj.constructor();

    for(var key in obj)
        temp[key] = clone(obj[key]);
    return temp;
}

function sanitizeJSON(data)
{
	for(p in data)
	{
		var t=(typeof data[p]);
		if(t=="string")
		{
			data[p]=data[p].escapeHTML();
		}
		else if(t=="object")
		{
			data[p]=sanitizeJSON(data[p]);
		}
	}
	return data;
}

getJSON = function(action, parameters, callback)
{
	var cb=callback;
	var t_action=action;
	
	if(t_action!="isimageready" && t_action!="piegraph" && t_action!="usagegraph")
	{
		g.last_action=action;
	}
	
	new window.Ajax.Request(getURL(action, parameters),
		{
			method: 'get',
			onComplete: function(transport)
			{
				var j;
				if(window.JSON)
					j=JSON.parse(transport.responseText);
				else
					j=transport.responseText.evalJSON(true);
					
				j=sanitizeJSON(j);
				
				if(j.error && j.error==1)
				{
				    g.session_timeout_cb();
				}
				else
				{
					if(t_action!="isimageready" && t_action!="piegraph" && t_action!="usagegraph")
					{
						g.last_function=cb;
						g.last_data=clone(j);
					}
				    cb(j);
				}
			},
			onException: function(e,ex)
			{
				throw ex;
			}
		});
}

g.tmpls={};

loadTmpl = function(name, callback)
{
	var cb=callback;
	
	if(g.tmpls[name])
	{
		cb(g.tmpls[name]);
		return;
	}
	
	new window.Ajax.Request(getURL("tmpl", "name="+name),
		{
			method: 'get',
			onComplete: function(transport)
			{
				g.tmpls[name]=new Template(transport.responseText);
				cb(g.tmpls[name]);
			}
		});
}

function loadGraph(action, parameters, pDivid)
{
	var divid=pDivid;
	var img_id=-1;
	var f=this;
	
	this.init_cb = function(data)
	{
		img_id=data.image_id;
		
		setTimeout(f.update_graph, 100);
	}
	
	this.update_graph = function()
	{
		getJSON("isimageready", "image_id="+img_id, f.update);
	}
	
	this.update = function(data)
	{
		if(data.image_ready)
		{
			I(divid).innerHTML="<img src=\"x?a=getimage&image_id="+img_id+"&ses="+g.session+"\" alt=\"Graph\" />";
		}
		else
		{
			setTimeout(f.update_graph, 100);
		}
	}
	
	getJSON(action, parameters, this.init_cb);
}

function hasTemplate(name)
{
	return g.tmpls[name]!=null;
}

function getTemplate(name)
{
	return g.tmpls[name];
}

function checkString(str)
{
	for(var i=0;i<str.length;++i)
	{
		var ch=str.charCodeAt(i);
		var ok=false;
		if( ch>=48 && ch <=57)
			ok=true;
		else if( ch >=65 && ch <=90 )
			ok=true;
		else if( ch >=97 && ch <=122 )
			ok=true;
		else if( ch==32 )
			ok=true;

		if( ok==false )
			return false;				
	}
	return true;
}

function LoadScript(url)
{
   var e = document.createElement("script");
   e.src = url;
   e.type="text/javascript";
   document.getElementsByTagName("head")[0].appendChild(e); 
}

function format_date(d)
{
	var wt=d.getDate();
	if( wt<10 )
		wt="0"+wt;
	var m=d.getMonth();
	++m;
	if( m<10 )
		m="0"+m;
	var j=d.getFullYear();
	j-=2000;
	if( j<10 )
		j="0"+j;
	
	var h=d.getHours();
	if( h<10 ) h="0"+h;
	
	var min=d.getMinutes();
	if( min<10 )
		min="0"+min;
	
	return wt+"."+m+"."+j+" "+h+":"+min;
}

function format_size(s)
{
	var suffix="bytes";
	if(s>1024)
	{
		s/=1024.0;
		suffix="KB";
	}
	if(s>1024)
	{
		s/=1024.0;
		suffix="MB"
	}
	if(s>1024)
	{
		s/=1024.0;
		suffix="GB";
	}
	if(s>1024)
	{
		s/=1024.0;
		suffix="TB";
	}
	
	s*=100;
	s=Math.round(s);
	s/=100.0;
	return s+" "+suffix;
}

function tmpl_replace(data, key, content)
{
	var idx1=data.indexOf("<!--"+key+"{-->");
	if( idx1!=-1 )
	{
		var idx2=data.indexOf("<!--}"+key+"-->", idx1);
		if( idx2!=-1 )
		{
			var stoff=idx1+key.length+8;
			var df=data.substr(0, stoff);
			var da=data.substr(idx2);
			
			return df+content+da;
		}
	}
}

function dump(arr,level) {
var dumped_text = "";
if(!level) level = 0;

var level_padding = "";
for(var j=0;j<level+1;j++) level_padding += "    ";

if(typeof(arr) == 'object') { 
 for(var item in arr) {
  var value = arr[item];
 
  if(typeof(value) == 'object') { 
   dumped_text += level_padding + "'" + item + "' ...\n";
   dumped_text += dump(value,level+1);
  } else {
   dumped_text += level_padding + "'" + item + "' => \"" + value + "\"\n";
  }
 }
} else { 
 dumped_text = "===>"+arr+"<===("+typeof(arr)+")";
}
return dumped_text;
} 

function randomString()
{
	var chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXTZabcdefghiklmnopqrstuvwxyz";
	var string_length = 50;
	var randomstring = '';
	for (var i=0; i<string_length; i++) {
		var rnum = Math.floor(Math.random() * chars.length);
		randomstring += chars.substring(rnum,rnum+1);
	}
	return randomstring;
}

function validate_text_nonempty(a)
{
	for(var i=0;i<a.length;++i)
	{
		if(I(a[i]).value.length==0)
		{
			if(trans["validate_err_empty_"+a[i]])
			{
				alert(trans["validate_err_empty_"+a[i]]);
			}
			else
			{
				alert( (new Template(trans["validate_text_empty"])).evaluate({name: trans["validate_name_"+a[i]]}));
			}
			I(a[i]).focus();
			return false;
		}
	}
	return true;
}

function isInt(x)
{
	var y=parseInt(x);
	if (isNaN(y)) return false;
	return x==y && x.toString()==y.toString();
} 

function validate_text_int(a)
{
	for(var i=0;i<a.length;++i)
	{
		if(!isInt(I(a[i]).value))
		{
			if(trans["validate_err_notint_"+a[i]])
			{
				alert(trans["validate_err_notint_"+a[i]]);
			}
			else
			{
				alert( (new Template(trans["validate_text_notint"])).evaluate({name: trans["validate_name_"+a[i]]}));
			}
			I(a[i]).focus();
			return false;
		}
	}
	return true;
}

function validate_text_regex(a)
{
	for(var i=0;i<a.length;++i)
	{
		if(!a[i].regexp.test(I(a[i].id).value))
		{
			if(trans["validate_err_notregexp_"+a[i].id])
			{
				alert(trans["validate_err_notregexp_"+a[i].id]);
			}
			else
			{
				alert( (new Template(trans["validate_text_notregexp"])).evaluate({name: trans["validate_name_"+a[i].id]}));
			}
			I(a[i].id).focus();
			return false;
		}
	}
	return true;
}
