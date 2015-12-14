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
if( navigator.appName=="Microsoft Internet Explorer")
{
	g.ie=true;
}
else if (navigator.appName == 'Netscape' &&
	(navigator.userAgent.indexOf("Edge")!=-1
	|| navigator.userAgent.indexOf("Trident")!=-1) ) //grr
{
	g.ie=true;
}

if (!Date.now) { Date.now = function now() { return +(new Date); }; } 

if(typeof String.prototype.trim !== 'function') {
  String.prototype.trim = function() {
    return this.replace(/^\s+|\s+$/g, ''); 
  }
}

function trans(str)
{
	if(typeof curr_trans[str]=="undefined")
		return translations.en[str];
	else
		return curr_trans[str];
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
		return '&iernd='+Date.now()+rnd;
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
	$('body').addClass('wait');
}
	
function ResetCursor()
{
	$('body').removeClass('wait');
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

function clone(obj){
    if(obj == null || typeof(obj) != 'object')
        return obj;

    var temp = obj.constructor();

    for(var key in obj)
        temp[key] = clone(obj[key]);
    return temp;
}

function escapeHTML(s)
{
	return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#x27;').replace(/\//g,'&#x2F;');
}

function unescapeHTML(s)
{
	return s.replace(/&amp;/g,'&').replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&quot;/g,'"').replace(/&#x27;/g,'\'').replace(/&#x2F;/g,'/');
}

function sanitizeJSON(data)
{
	for(p in data)
	{
		var t=(typeof data[p]);
		if(t=="string")
		{
			data[p]=escapeHTML(data[p]);
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
	
	$.ajax(
	{ url: getURL(action, parameters),
	  dataType: "json"
	}
	).done(function(data)
		{			
			data=sanitizeJSON(data);
			
			if(data.error && data.error==1)
			{
				g.session_timeout_cb();
			}
			else
			{
				if(t_action!="isimageready" && t_action!="piegraph" && t_action!="usagegraph")
				{
					g.last_function=cb;
					g.last_data=clone(data);
				}
				try
				{
					cb(data);
				}
				catch(e)
				{
					console.log(e);
				}
			}
		});
}

g.tmpls={};

function loadGraph(action, parameters, pDivid, pGraphdata, pAddHtml)
{
	var divid=pDivid;
	var f=this;
	var graphdata=pGraphdata;
	var addHtml = pAddHtml;
	
	this.init_cb = function(data)
	{	
		I(divid).style.width = "100%";
		I(divid).style.height = graphdata.height+"px";
		I(divid).innerHTML="<span id='"+divid+"_plot' style='width: 100%; height: 100%; display: inline-block'></span>"+addHtml;
		I(divid).style.display="inline-block";
		if(graphdata.pie)
		{
			var jqdata = [];
			for(var i=0;i<data.data.length;++i)
			{
				var obj=data.data[i];
				jqdata.push([obj.label, obj.data]);
			}
			
			var piegraph = jQuery.jqplot (divid+"_plot", [jqdata],
			{
				  seriesDefaults: {
					renderer: jQuery.jqplot.PieRenderer,
					rendererOptions: {
					  showDataLabels: true,
					  highlightMouseOver: true,
					  padding: 2,
					  sliceMargin: 2
					}
				  },
				  legend: {
					show:true,
					location: 'e',
					rendererOptions: {numberRows: 10, numberColumns: 2}
				  },
				  title: graphdata.title,
				  highlighter: {
					show: true,
					formatString:'%s', 
					tooltipLocation:'sw', 
					useAxesFormatters:false
				  },
				  cursor: {
					show: true
				  }
			});
			
		}
		else
		{
			var series = [];
			var ticks = [];
			for(var i=0;i<data.data.length;++i)
			{
				var obj=data.data[i];
				series.push([obj.xlabel, obj.data]);
			}
			
			var plot1 = $.jqplot(divid+"_plot", [series], {
				legend: {
					show: false
				},
				axes: {
					xaxis: {
						renderer:$.jqplot.DateAxisRenderer,
						tickOptions: {
							formatString: graphdata.dateFormat
						},
						label: graphdata.colname1
					},
					yaxis: {
						pad: 1.05,
						tickOptions: {formatString: '%d'+data.ylabel},
						label: graphdata.colname2
					}
				},
				title: graphdata.title,
				highlighter: {
					show: true,
					formatString: graphdata.colname1+": %s "+graphdata.colname2+": %s"
				},
				cursor: {
					show: false
				}
			});
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

function LoadScript(url, id)
{
	if(!I(id))
	{
	   var e = document.createElement("script");
	   e.src = url;
	   e.type="text/javascript";
	   e.id=id;
	   document.getElementsByTagName("head")[0].appendChild(e);
	}
}

function determine_date_format()
{
	//Create a known date string
	var y = new Date(2013, 9, 25);
	var lds = y.toLocaleDateString();

	//search for the position of the year, day, and month
	var yPosi = lds.search("2013");
	var dPosi = lds.search("25");
	var mPosi = lds.search("10");

	// try to determine date separator
	var dateSeperator = "/";
	var pointPos = lds.indexOf(".");
	if (pointPos != -1)
	dateSeperator = ".";

	//Sometimes the month is displayed by the month name so guess where it is
	if(mPosi == -1)
	{
		mPosi = lds.search("9");
		if(mPosi == -1)
		{
			//if the year and day are not first then maybe month is first
			if(yPosi != 0 && dPosi != 0)
			{
				mPosi = 0;
			}
			//if year and day are not last then maybe month is last
			else if((yPosi+4 <  lds.length) && (dPosi+2 < lds.length)){
				mPosi = Infinity;
			}
			//otherwist is in the middle
			else  if(yPosi < dPosi){
				mPosi = ((dPosi - yPosi)/2) + yPosi;            
			}else if(dPosi < yPosi){
				mPosi = ((yPosi - dPosi)/2) + dPosi;
			}   
		}
	}
	
	var formatString="";
	
	var order = [yPosi, dPosi, mPosi];
	order.sort(function(a,b){return a-b});

	for(i=0; i < order.length; i++)
	{
		if(i>0)
			formatString+=dateSeperator;
			
		if(order[i] == yPosi)
		{
			formatString += "YYYY";
		}else if(order[i] == dPosi){
			formatString += "DD";
		}else if(order[i] == mPosi){
			formatString += "MM";
		}
	}
	
	g.dateFormatString = formatString;
}

determine_date_format();

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
		
	return g.dateFormatString.replace("YYYY", j).
			replace("MM", m).replace("DD", wt) +
				" "+h+":"+min;
}

function format_unix_timestamp(ts)
{
	return format_date(new Date(ts*1000));
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
		suffix="MB";
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

function format_time_seconds(t, s)
{
	var ret="";
	var x_min=60;
	var x_hour=x_min*60;
	var x_day=x_hour*24;
	var x_week=x_day*7;
	var x_month=x_week*4.345;
	var x_year=x_month*12;
	var x_tp={year: x_year, month: x_month, week: x_week, day: x_day, hour: x_hour, min: x_min};
	
	var neg=false;
	if(t<0)
	{
		neg=true;
		t*=-1;
	}

	for(x in x_tp)
	{
		var y=Math.floor(t/x_tp[x]);
		if(y>0)
		{
			if(ret!="")
				ret+=" ";
				
			if(neg)
				ret+="-";
				
			ret+=y;
			var c_s=(s==true);
			if(c_s)
			{
				if(x=="hour") ret+="h";
				else if(x=="min") ret+="m";
				else c_s=false;
			}
			
			if(!c_s)
			{
				ret+=" ";
				if(y>1)
					ret+=trans(x+"s");
				else
					ret+=trans(x);
			}
			
				
			t-=x_tp[x]*y;
		}
	}
	
	return ret;
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
	
	var array = new Uint32Array(string_length);
	if(window.crypto && window.crypto.getRandomValues(array))
	{
		for (var i=0; i<string_length; i++) {
			randomstring += chars.charAt(array[i]%chars.length);
		}
		return randomstring;
	}
	
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
			if(trans("validate_err_empty_"+a[i]))
			{
				alert(trans("validate_err_empty_"+a[i]));
			}
			else
			{
				alert( dustCompileRender(trans("validate_text_empty"), {name: trans("validate_name_"+a[i])}));
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

function isFloat(x)
{
	var y=parseFloat(x);
	if (isNaN(y)) return false;
	return true;
}

function validate_text_int(a)
{
	for(var i=0;i<a.length;++i)
	{
		if(I(a[i]) && !isInt(I(a[i]).value))
		{
			if(trans("validate_err_notint_"+a[i]))
			{
				alert(trans("validate_err_notint_"+a[i]));
			}
			else
			{
				alert( dustCompileRender(trans("validate_text_notint"), {name: trans("validate_name_"+a[i])}));
			}
			I(a[i]).focus();
			return false;
		}
	}
	return true;
}

function validate_text_float(a)
{
	for(var i=0;i<a.length;++i)
	{
		if(!isFloat(I(a[i]).value))
		{
			if(trans("validate_err_notfloat_"+a[i]))
			{
				alert(trans("validate_err_notfloat_"+a[i]));
			}
			else
			{
				alert( dustCompileRender(trans("validate_text_notint"), {name: trans("validate_name_"+a[i])}));
			}
			I(a[i]).focus();
			return false;
		}
	}
	return true;
}

function validate_text_int_or_empty(a)
{
	for(var i=0;i<a.length;++i)
	{
		if(!isInt(I(a[i]).value) && I(a[i]).value!="-" && I(a[i]).value!="")
		{
			if(trans("validate_err_notint_"+a[i]))
			{
				alert(trans("validate_err_notint_"+a[i]));
			}
			else
			{
				alert( dustCompileRender(trans("validate_text_notint"), {name: trans("validate_name_"+a[i])}));
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
			var errid=a[i].id;
			if(a[i].errid)
			{
				errid=a[i].errid;
			}
			if(trans("validate_err_notregexp_"+errid))
			{
				alert(trans("validate_err_notregexp_"+errid));
			}
			else if( trans("validate_text_notregexp") )
			{
				alert( dustCompileRender(trans("validate_text_notregexp"), {name: trans("validate_name_"+a[i].id)}));
			}
			else
			{
				alert("Field format wrong!");
			}
			I(a[i].id).focus();
			return false;
		}
	}
	return true;
}

function getCheckboxValue(v)
{
	if(v) return "checked=\"checked\"";
	else return "";
}

function addSelectSelected(options, name, params)
{
	var selected_option=params[name];
	for(var i=0;i<options.length;++i)
	{
		if(options[i]==selected_option)
		{
			params[name+"_"+i]="selected=\"selected\"";
			return params;
		}
	}
	return params;
}

function show_hide_column(table_id, col_no, do_show)
{
    var rows = document.getElementById(table_id).rows;

    for (var row = 0; row < rows.length; row++) {
        var cols = rows[row].cells;
        if (col_no >= 0 && col_no < cols.length) {
            cols[col_no].style.display = do_show ? '' : 'none';
        }
    }
}
String.prototype.trim = function() {
    return this.replace(/^\s+|\s+$/g, "");
};

function dustCompileRender(template, data)
{
	var compiled = dust.compile(template, "tmp");
	dust.loadSource(compiled);
	return dustRender("tmp", data);
}

function dustRender(template, data)
{
	var result;
	dust.render(template, data, function(err, res) {
		if(err) throw err;
	   result = res;
	});
	return result;
}
function getISODatestamp()
{
	var d = new Date();
	var m = d.getMonth() + 1;
	if(m<10)
		m = "0" +m;
	var day = d.getDate();
	if( day<10)
		day = "0"+day;
	return d.getFullYear() + '-' + m + '-' + day;
}

function base64_decode_dash(b)
{
	return $.base64.decode(b.replace(/-/g, "="));
}

/*
 * jQuery BBQ: Back Button & Query Library - v1.2.1 - 2/17/2010
 * http://benalman.com/projects/jquery-bbq-plugin/
 * 
 * Copyright (c) 2010 "Cowboy" Ben Alman
 * Dual licensed under the MIT and GPL licenses.
 * http://benalman.com/about/license/
 */
!function(deparam){if(typeof require==="function"&&typeof exports==="object"&&typeof module==="object"){var jquery=require("jquery");module.exports=deparam(jquery)}else if(typeof define==="function"&&define.amd){define(["jquery"],function(jquery){return deparam(jquery)})}else{var global=(false||eval)("this");global.deparam=deparam(jQuery)}}(function($){return function(params,coerce){var obj={},coerce_types={"true":!0,"false":!1,"null":null};$.each(params.replace(/\+/g," ").split("&"),function(j,v){var param=v.split("="),key=decodeURIComponent(param[0]),val,cur=obj,i=0,keys=key.split("]["),keys_last=keys.length-1;if(/\[/.test(keys[0])&&/\]$/.test(keys[keys_last])){keys[keys_last]=keys[keys_last].replace(/\]$/,"");keys=keys.shift().split("[").concat(keys);keys_last=keys.length-1}else{keys_last=0}if(param.length===2){val=decodeURIComponent(param[1]);if(coerce){val=val&&!isNaN(val)?+val:val==="undefined"?undefined:coerce_types[val]!==undefined?coerce_types[val]:val}if(keys_last){for(;i<=keys_last;i++){key=keys[i]===""?cur.length:keys[i];cur=cur[key]=i<keys_last?cur[key]||(keys[i+1]&&isNaN(keys[i+1])?{}:[]):val}}else{if($.isArray(obj[key])){obj[key].push(val)}else if(obj[key]!==undefined){obj[key]=[obj[key],val]}else{obj[key]=val}}}else if(key){obj[key]=coerce?undefined:""}});return obj}});
