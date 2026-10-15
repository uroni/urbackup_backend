// Describes the interval and min/max number of backups settings in plain text
// and warns about combinations the server cannot keep.

g_backup_summary = {};

g_backup_summary.max_slots = 40000;
g_backup_summary.max_backups = 4000;
// Backups looked at while running the schedule, to keep the page responsive
g_backup_summary.max_work = 20000000;

g_backup_summary.texts = {
	file: {
		what_full: "full file backups",
		what_incr: "incremental file backups",
		sched_both: "A full file backup runs about every {full} and an incremental file backup about every {incr} in between.",
		sched_all_full: "Every scheduled file backup is a full file backup, about every {incr}.",
		sched_incr: "An incremental file backup runs about every {incr}. Full file backups are not scheduled, only the first file backup is a full one.",
		sched_full: "A full file backup runs about every {full}. Incremental file backups are disabled.",
		sched_none: "No file backups are scheduled.",
		keep: "The server keeps at most {max_full} full and {max_incr} incremental file backups. Each of them can be restored on its own.",
		warn_full_le_incr: "The interval for full file backups ({full}) is not longer than the interval for incremental file backups ({incr}). No incremental file backups are made and a full file backup runs every {incr}."
	},
	image: {
		what_full: "full image backups",
		what_incr: "incremental image backups",
		sched_both: "A full image backup runs about every {full} and an incremental image backup about every {incr} in between.",
		sched_all_full: "Every scheduled image backup is a full image backup, about every {incr}.",
		sched_incr: "An incremental image backup runs about every {incr}. Full image backups are not scheduled, only the first image backup of a volume is a full one.",
		sched_full: "A full image backup runs about every {full}. Incremental image backups are disabled.",
		sched_none: "No image backups are scheduled.",
		keep: "The server keeps at most {max_full} full and {max_incr} incremental image backups per volume. An incremental image backup can only be restored together with the image backups it is based on, so those are not deleted before it.",
		warn_full_le_incr: "The interval for full image backups ({full}) is not longer than the interval for incremental image backups ({incr}). No incremental image backups are made and a full image backup runs every {incr}."
	},
	sched_varies: "The intervals depend on the time of day or week, so the schedule is not described here.",
	sched_note: "Backups only start while the client is online and inside the backup window, so they can run later than that.",
	reach_both: "If every scheduled backup runs, every backup of the last {fine} can be restored. Further back only some of them are left, the oldest one is about {all} old.",
	reach_one: "If every scheduled backup runs, the backups reach back about {all}.",
	space: "If the backup storage runs out of space, the oldest backups are deleted earlier, down to {min_full} full and {min_incr} incremental ones.",
	warn_min_max: "The minimal number of {what} ({min}) is larger than the maximal number ({max}). Never more than {max} are kept, and none of them can be deleted if the backup storage runs out of space.",
	warn_max_zero: "The maximal number of {what} is 0, so they are deleted again by the next cleanup.",
	incr_only_image: "All incremental image backups are based, directly or through earlier ones, on the first full image backup, so that one is never deleted.",
	warn_newest: "With these settings the cleanup deletes the newest incremental image backups instead of the oldest ones. Incremental image backups are based on each other here, and one that others are based on is only deleted together with them. Keep at least {n} incremental image backups or base them on the last full image backup (incremental image style).",
	warn_full_max: "The maximal number of full image backups ({max}) cannot always be kept. A full image backup is only deleted together with the incremental image backups based on it, and the minimal number of incremental image backups ({min_incr}) prevents that. Expect up to {n} full image backups.",
	warn_incr_max: "The maximal number of incremental image backups ({max}) cannot always be kept, because an incremental image backup that others are based on is only deleted together with them. Expect up to {n} incremental image backups.",
	hour: "1 hour",
	hours: "{n} hours",
	day: "1 day",
	days: "{n} days",
	months: "{n} months",
	years: "{n} years"
};

g_backup_summary.msg = function(text, params)
{
	var ret = text;
	if(typeof trans == "function")
	{
		var t = trans(text);
		if(typeof t == "string") ret = t;
	}

	if(params)
	{
		for(var key in params)
		{
			if(params.hasOwnProperty(key))
			{
				ret = ret.split("{"+key+"}").join(params[key]);
			}
		}
	}
	return ret;
}

g_backup_summary.duration = function(seconds)
{
	var texts = g_backup_summary.texts;
	var msg = g_backup_summary.msg;
	var round1 = function(v) { return Math.round(v*10)/10; };

	var hours = seconds/3600;
	if(hours<48)
	{
		var n = round1(hours);
		return n==1 ? msg(texts.hour) : msg(texts.hours, {n: n});
	}
	var days = hours/24;
	if(days<60)
	{
		var n = round1(days);
		return n==1 ? msg(texts.day) : msg(texts.days, {n: n});
	}
	if(days<730)
	{
		return msg(texts.months, {n: Math.round(days/30.44)});
	}
	return msg(texts.years, {n: round1(days/365.25)});
}

// Interval as entered in the settings: number in units of unit_seconds.
// Returns seconds, -1 if disabled, "varies" for intervals with time windows
// and null if there is no usable number.
g_backup_summary.parseInterval = function(text, disabled, unit_seconds)
{
	if(disabled) return -1;

	text = (""+text).replace(/^\s+|\s+$/g, "");
	if(text.indexOf("@")!=-1 || text.indexOf(";")!=-1) return "varies";
	if(!/^-?[\d.]+$/.test(text)) return null;

	var val = parseFloat(text);
	if(isNaN(val) || val==0) return null;
	if(val<0) return -1;
	return val*unit_seconds;
}

g_backup_summary.parseCount = function(text)
{
	text = (""+text).replace(/^\s+|\s+$/g, "");
	if(!/^\d+$/.test(text)) return null;
	return parseInt(text, 10);
}

// Runs the backup schedule and the cleanup of the server (ServerCleanupThread)
// for one client and volume until the number of backups has settled.
// cfg: incr, full (seconds, -1 if disabled), max_full, max_incr, min_incr,
//      image, to_last (incremental image backups based on the last image backup)
// Returns null if that would take too long, else
// {fulls_peak, incr_peak, fine, all (seconds), cycle (seconds), per_cycle, newest_lost}
g_backup_summary.simulate = function(cfg)
{
	if(cfg.incr<=0 && cfg.full<=0) return null;

	var step = cfg.incr>0 ? cfg.incr : cfg.full;
	// Every cycle_slots-th backup is a full one; 0: only the first one
	var cycle_slots = 1;
	if(cfg.incr>0 && cfg.full>0)
		cycle_slots = Math.max(1, Math.ceil(cfg.full/cfg.incr));
	else if(cfg.incr>0)
		cycle_slots = 0;

	var per_cycle = cycle_slots>0 ? cycle_slots-1 : 1;
	var cycles = cfg.max_full + Math.ceil((cfg.max_incr + cfg.min_incr)/Math.max(per_cycle, 1)) + 6;
	var slots = cycles*Math.max(cycle_slots, 1);
	var measure_slots = 2*Math.max(cycle_slots, 1) + 2;
	if(cycle_slots==0)
	{
		slots = 2*(cfg.max_incr + cfg.min_incr) + 50;
		measure_slots = cfg.max_incr + cfg.min_incr + 10;
	}

	if(slots>g_backup_summary.max_slots
		|| cfg.max_full + cfg.max_incr>g_backup_summary.max_backups)
	{
		return null;
	}

	// Oldest first: {t: slot, full: bool, ref: slot of the backup it is based on or -1}
	var backups = [];
	var work = 0;

	var count = function(full)
	{
		work += backups.length;
		var c = 0;
		for(var i=0;i<backups.length;++i)
			if(backups[i].full==full) ++c;
		return c;
	};

	var oldest = function(full, notit)
	{
		work += backups.length;
		var ret = [];
		for(var i=0;i<backups.length;++i)
			if(backups[i].full==full && notit.indexOf(backups[i].t)==-1) ret.push(backups[i]);
		return ret;
	};

	var remove = function(backup)
	{
		backups.splice(backups.indexOf(backup), 1);
	};

	// ServerCleanupThread::removeImage with remove_references
	var removeImage = function(backup, del_incr_in_stack)
	{
		work += backups.length;
		if(work>g_backup_summary.max_work) return false;

		var refs = [];
		for(var i=0;i<backups.length;++i)
			if(!backups[i].full && backups[i].ref==backup.t) refs.push(backups[i]);

		for(var i=0;i<refs.length;++i)
		{
			if(count(false) - del_incr_in_stack - cfg.min_incr<=0)
				return false;
			if(!removeImage(refs[i], del_incr_in_stack+1))
				return false;
		}

		remove(backup);
		return true;
	};

	// ServerCleanupThread::cleanup_one_imagebackup_client
	var cleanupOneImage = function()
	{
		var notit = [];
		var cand = oldest(true, notit);
		while(cand.length>cfg.max_full)
		{
			if(removeImage(cand[0], 0)) return true;
			notit.push(cand[0].t);
			cand = oldest(true, notit);
		}

		notit = [];
		cand = oldest(false, notit);
		while(cand.length>cfg.max_incr)
		{
			if(removeImage(cand[0], 1)) return true;
			notit.push(cand[0].t);
			cand = oldest(false, notit);
		}
		return false;
	};

	// ServerCleanupThread::cleanup_one_filebackup_client
	var cleanupFiles = function()
	{
		var cand = oldest(true, []);
		while(cand.length>cfg.max_full
			&& !(cand.length==1 && cfg.max_incr>0 && cfg.incr>0 && count(false)==0))
		{
			remove(cand[0]);
			cand = oldest(true, []);
		}

		cand = oldest(false, []);
		while(cand.length>cfg.max_incr)
		{
			remove(cand[0]);
			cand = oldest(false, []);
		}
	};

	var ret = { fulls_peak: 0, incr_peak: 0, fine: -1, all: -1,
		cycle: Math.max(cycle_slots, 1)*step, per_cycle: per_cycle, newest_lost: false };

	for(var t=0;t<slots;++t)
	{
		var fulls = oldest(true, []);
		var last_full = fulls.length>0 ? fulls[fulls.length-1].t : -1;
		// An incremental image backup needs a full one, an incremental file backup any backup
		var need_full = cfg.image ? last_full==-1 : backups.length==0;

		if(need_full || (cycle_slots>0 && (last_full==-1 || t-last_full>=cycle_slots)))
		{
			backups.push({t: t, full: true, ref: -1});
		}
		else
		{
			backups.push({t: t, full: false, ref: cfg.to_last ? backups[backups.length-1].t : last_full});
		}

		if(cfg.image)
		{
			while(cleanupOneImage()) {}
		}
		else
		{
			cleanupFiles();
		}

		if(work>g_backup_summary.max_work)
			return null;

		if(t<slots-measure_slots)
			continue;

		if(backups.length==0 || backups[backups.length-1].t!=t)
			ret.newest_lost = true;

		ret.fulls_peak = Math.max(ret.fulls_peak, count(true));
		ret.incr_peak = Math.max(ret.incr_peak, count(false));

		var all = backups.length>0 ? t-backups[0].t : 0;
		var fine = 0;
		for(var i=backups.length-1;i>=0 && backups[i].t==t-fine;--i)
			++fine;
		fine = Math.max(fine-1, 0);

		if(ret.all==-1 || all<ret.all) ret.all = all;
		if(ret.fine==-1 || fine<ret.fine) ret.fine = fine;
	}

	ret.fine *= step;
	ret.all *= step;
	return ret;
}

// settings: incr, full (see parseInterval), max_full, min_full, max_incr, min_incr
//           (see parseCount), image, to_last (array with the incremental image styles in use)
// Returns {info: [], warn: []} with the texts to show
g_backup_summary.describe = function(settings)
{
	var texts = g_backup_summary.texts;
	var kind = settings.image ? texts.image : texts.file;
	var msg = g_backup_summary.msg;
	var duration = g_backup_summary.duration;
	var ret = { info: [], warn: [] };

	var incr = settings.incr;
	var full = settings.full;
	var has_schedule = false;

	if(incr=="varies" || full=="varies")
	{
		ret.info.push(msg(texts.sched_varies));
	}
	else if(incr!==null && full!==null)
	{
		has_schedule = incr>0 || full>0;
		if(incr>0 && full>0 && full<=incr)
		{
			ret.info.push(msg(kind.sched_all_full, {incr: duration(incr)}));
			ret.warn.push(msg(kind.warn_full_le_incr, {full: duration(full), incr: duration(incr)}));
		}
		else if(incr>0 && full>0)
		{
			ret.info.push(msg(kind.sched_both, {full: duration(Math.ceil(full/incr)*incr), incr: duration(incr)}));
		}
		else if(incr>0)
		{
			ret.info.push(msg(kind.sched_incr, {incr: duration(incr)}));
		}
		else if(full>0)
		{
			ret.info.push(msg(kind.sched_full, {full: duration(full)}));
		}
		else
		{
			ret.info.push(msg(kind.sched_none));
		}

		if(has_schedule)
			ret.info.push(msg(texts.sched_note));
	}

	var counts = [settings.max_full, settings.min_full, settings.max_incr, settings.min_incr];
	for(var i=0;i<counts.length;++i)
	{
		if(counts[i]===null) return ret;
	}

	ret.info.push(msg(kind.keep, {max_full: settings.max_full, max_incr: settings.max_incr}));

	var full_used = has_schedule && (full>0 || incr>0);
	var incr_used = has_schedule && incr>0 && !(full>0 && full<=incr);

	if(settings.min_full>settings.max_full)
		ret.warn.push(msg(texts.warn_min_max, {what: msg(kind.what_full), min: settings.min_full, max: settings.max_full}));
	if(settings.min_incr>settings.max_incr)
		ret.warn.push(msg(texts.warn_min_max, {what: msg(kind.what_incr), min: settings.min_incr, max: settings.max_incr}));
	if(settings.max_full==0 && full_used)
		ret.warn.push(msg(texts.warn_max_zero, {what: msg(kind.what_full)}));
	if(settings.max_incr==0 && incr_used)
		ret.warn.push(msg(texts.warn_max_zero, {what: msg(kind.what_incr)}));

	var sim = null;
	if(has_schedule && settings.image && full<=0)
	{
		ret.info.push(msg(texts.incr_only_image));
	}
	else if(has_schedule)
	{
		var styles = settings.image ? settings.to_last : [false];
		for(var i=0;i<styles.length;++i)
		{
			var curr = g_backup_summary.simulate({incr: incr, full: full, max_full: settings.max_full,
				max_incr: settings.max_incr, min_incr: settings.min_incr, image: settings.image,
				to_last: styles[i]});

			if(curr===null)
			{
				sim = null;
				break;
			}

			if(sim===null)
			{
				sim = curr;
			}
			else
			{
				sim.fulls_peak = Math.max(sim.fulls_peak, curr.fulls_peak);
				sim.incr_peak = Math.max(sim.incr_peak, curr.incr_peak);
				sim.fine = Math.min(sim.fine, curr.fine);
				sim.all = Math.min(sim.all, curr.all);
				sim.newest_lost = sim.newest_lost || curr.newest_lost;
			}
		}
	}

	if(sim!==null)
	{
		var step = incr>0 ? incr : full;
		// Without scheduled full backups the first one stays, however old it gets
		if(full<=0) sim.all = sim.fine;

		if(sim.all>0 && sim.fine>0 && sim.fine+step<sim.all)
			ret.info.push(msg(texts.reach_both, {fine: duration(sim.fine), all: duration(sim.all)}));
		else if(sim.all>0)
			ret.info.push(msg(texts.reach_one, {all: duration(sim.all)}));

		if(settings.image && sim.newest_lost && incr_used && settings.max_incr>0)
			ret.warn.push(msg(texts.warn_newest, {n: sim.per_cycle}));

		if(settings.image && sim.fulls_peak>settings.max_full && settings.max_full>0)
			ret.warn.push(msg(texts.warn_full_max, {max: settings.max_full, min_incr: settings.min_incr, n: sim.fulls_peak}));
		if(settings.image && sim.incr_peak>settings.max_incr && settings.max_incr>0 && !sim.newest_lost)
			ret.warn.push(msg(texts.warn_incr_max, {max: settings.max_incr, n: sim.incr_peak}));
	}

	if(settings.min_full<=settings.max_full && settings.min_incr<=settings.max_incr
		&& (settings.min_full<settings.max_full || settings.min_incr<settings.max_incr))
	{
		ret.info.push(msg(texts.space, {min_full: settings.min_full, min_incr: settings.min_incr}));
	}

	return ret;
}

g_backup_summary.inputs = [
	{ id: "file_backup_summary", image: false, incr: "update_freq_incr", incr_unit: 60*60,
		full: "update_freq_full", full_unit: 24*60*60, counts: "file" },
	{ id: "image_backup_summary", image: true, incr: "update_freq_image_incr", incr_unit: 24*60*60,
		full: "update_freq_image_full", full_unit: 24*60*60, counts: "image" }
];

// Shows the description below the interval and number of backups settings
function updateBackupSummary()
{
	var value = function(id)
	{
		return I(id) ? I(id).value : "";
	};
	var interval = function(id, unit)
	{
		return g_backup_summary.parseInterval(value(id), I(id+"_disable") && I(id+"_disable").checked, unit);
	};

	for(var i=0;i<g_backup_summary.inputs.length;++i)
	{
		var input = g_backup_summary.inputs[i];
		if(!I(input.id)) continue;

		var to_last = [];
		var styles = ["local_incr_image_style", "internet_incr_image_style"];
		for(var j=0;j<styles.length;++j)
		{
			var curr = value(styles[j])=="to-last";
			if(I(styles[j]) && $.inArray(curr, to_last)==-1) to_last.push(curr);
		}
		if(to_last.length==0) to_last.push(false);

		var summary = g_backup_summary.describe({ image: input.image,
			incr: interval(input.incr, input.incr_unit), full: interval(input.full, input.full_unit),
			max_full: g_backup_summary.parseCount(value("max_"+input.counts+"_full")),
			min_full: g_backup_summary.parseCount(value("min_"+input.counts+"_full")),
			max_incr: g_backup_summary.parseCount(value("max_"+input.counts+"_incr")),
			min_incr: g_backup_summary.parseCount(value("min_"+input.counts+"_incr")),
			to_last: to_last });

		var html = "";
		if(summary.info.length>0)
		{
			html += "<div class=\"alert alert-info\" style=\"margin-bottom: 5px\">";
			for(var j=0;j<summary.info.length;++j)
			{
				html += (j>0 ? "<br/>" : "") + escapeHTML(summary.info[j]);
			}
			html += "</div>";
		}
		for(var j=0;j<summary.warn.length;++j)
		{
			html += "<div class=\"alert alert-warning\" style=\"margin-bottom: 5px\">" + escapeHTML(summary.warn[j]) + "</div>";
		}
		I(input.id).innerHTML = html;
	}
}

if(typeof module!="undefined")
{
	module.exports = g_backup_summary;
}
else
{
	$(document).on("keyup change", "#update_freq_incr, #update_freq_full, #update_freq_image_incr, #update_freq_image_full, "
		+ "#update_freq_incr_disable, #update_freq_full_disable, #update_freq_image_incr_disable, #update_freq_image_full_disable, "
		+ "#max_file_incr, #min_file_incr, #max_file_full, #min_file_full, "
		+ "#max_image_incr, #min_image_incr, #max_image_full, #min_image_full, "
		+ "#local_incr_image_style, #internet_incr_image_style", updateBackupSummary);
}
