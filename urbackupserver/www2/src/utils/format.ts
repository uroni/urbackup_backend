import { i18n } from "@lingui/core";

function determine_date_format()
{
	//Create a known date string
	var y = new Date(2013, 9, 25);
	var lds;
	if(navigator.languages && navigator.languages.length)
		lds = y.toLocaleDateString(navigator.languages[0]);
	else
		lds = y.toLocaleDateString();

	//search for the position of the year, day, and month
	var yPosi = lds.search("2013");
	var dPosi = lds.search("25");
	var mPosi = lds.search("10");
	
	if(dPosi==-1)
	{
		dPosi = lds.search("24");
		if(dPosi==-1)
		{
			dPosi = lds.search("26");
		}
	}

	// try to determine date separator
	var dateSeperator = "/";
	var pointPos = lds.indexOf(".");
	if (pointPos != -1)
		dateSeperator = ".";
	const dashPos = lds.indexOf("-");
	if (dashPos != -1)
		dateSeperator = "-";

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

	for(let i=0; i < order.length; i++)
	{
		if(i>0)
			formatString+=dateSeperator;
			
		if(order[i] == yPosi)
		{
			if(i==0)
				formatString += "YYYY";
			else
				formatString += "YY";
		}else if(order[i] == dPosi){
			formatString += "DD";
		}else if(order[i] == mPosi){
			formatString += "MM";
		}
	}
	
	return formatString;
}

const dateFormatString = determine_date_format();

export function format_date(d: Date)
{
	let wt : number|string=d.getDate();
	if( wt<10 )
		wt="0"+wt;
	let m : number|string =d.getMonth();
	++m;
	if( m<10 )
		m="0"+m;
	let j: number|string=d.getFullYear();
	j-=2000;
	if( j<10 )
		j="0"+j;
	
	let h:  number|string=d.getHours();
	if( h<10 ) h="0"+h;
	
	let min: number|string=d.getMinutes();
	if( min<10 )
		min="0"+min;
		
	return dateFormatString.replace("YYYY", ""+d.getFullYear()).
			replace("YY", ""+j).
			replace("MM", ""+m).replace("DD", ""+wt) +
				" "+h+":"+min;
}

export function format_unix_timestamp(ts: number)
{
	return format_date(new Date(ts*1000));
}

export function format_size(s: number)
{
	var suffix="bytes";
	if(s>1024)
	{
		s/=1024.0;
		suffix="KiB";
	}
	if(s>1024)
	{
		s/=1024.0;
		suffix="MiB";
	}
	if(s>1024)
	{
		s/=1024.0;
		suffix="GiB";
	}
	if(s>1024)
	{
		s/=1024.0;
		suffix="TiB";
	}
	
	s*=100;
	s=Math.round(s);
	s/=100.0;
	return s+" "+suffix;
}

export function format_size_bits(s: number)
{
	var suffix="bits";
	if(s>1000)
	{
		s/=1000.0;
		suffix="Kbit";
	}
	if(s>1000)
	{
		s/=1000.0;
		suffix="Mbit";
	}
	if(s>1000)
	{
		s/=1000.0;
		suffix="Gbit";
	}
	if(s>1000)
	{
		s/=1000.0;
		suffix="Tbit";
	}
	
	s*=100;
	s=Math.round(s);
	s/=100.0;
	const suffix_trans = i18n._(suffix);
	return s+" "+ (suffix_trans!=null ? suffix_trans : suffix);
}