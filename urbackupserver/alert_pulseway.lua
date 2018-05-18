--find largest (maximum of incr and full interval) interval for file and image backups
local file_interval = params.incr_file_interval
local full_file_interval = params.full_file_interval
if full_file_interval>0 and full_file_interval<file_interval
then
	file_interval = full_file_interval
end

local image_interval = params.incr_image_interval
local full_image_interval = params.full_image_interval
if full_image_interval>0 and full_image_interval<image_interval
then
	image_interval = full_image_interval
end

--ret=0 image and file backup ok
--ret=1 image ok, file backup not ok
--ret=2 image not ok, file backup ok
--ret=3 image and file backup not ok
local ret = 0
--Time in milliseconds to wait till this script is run next
local next_check_ms = 30*60*1000
local next_check_pulseway

if params.username~="" and (global_mem.next_instance_refresh==nil or global_mem.next_instance_refresh<time.monotonic_ms())
then
	local tbl = {
	  instance_id = params.instance_id,
	  name = params.instance_name,
	  group = params.instance_group,
	  description = params.instance_description,
	  next_refresh_interval_minutes = params.refresh_interval_minutes,
	  notify_when_offline = params.instance_notify_when_offline and "true" or "false"
	}
	local json = require("dkjson")
	local rc, http_ret, http_code, http_err = request_url(params.api_url .. "/systems", {
		method = "POST",
		content_type = "application/json",
		postdata = json.encode (tbl, { indent = true }),
		basic_authorization = params.username .. ":" .. params.password
	})
	
	if rc
	then
		next_check_ms = params.refresh_interval_minutes*60*1000 - 30*1000
		global_mem.next_instance_refresh = time.monotonic_ms() + next_check_ms
		next_check_pulseway = next_check_ms
	else
		log("HTTP POST to "..params.api_url .. "/systems".." failed with http code ".. http_code .. ". "..http_err ..". Returning "..http_ret, LL_ERROR)
		next_check_ms = 30*1000
		next_check_pulseway = next_check_ms
		global_mem.next_instance_refresh = time.monotonic_ms() + next_check_ms
	end
	log("Next refresh: "..global_mem.next_instance_refresh)
end

function pretty_time_append(seconds, name, amount, result)
	if seconds>amount
	then
		local t = math.floor(seconds/amount)
		if result~=""
		then
			result = result .. " "
		end
		result = result .. t .. name
		seconds = seconds - t*amount
	end
	
	return seconds, result
end

--Function to pretty print a duration in seconds
function pretty_time(seconds)
	result = ""
	seconds, result = pretty_time_append(seconds, " month", 30*60*60*24, result)
	seconds, result = pretty_time_append(seconds, " days", 60*60*24, result)
	seconds, result = pretty_time_append(seconds, "h", 60*60, result)
	seconds, result = pretty_time_append(seconds, "min", 60, result)
	if result==""
	then
		result=seconds.. "s"
	end
	return result
end

function pulseway_notify(subj, msg)
	local tbl = {
	  instance_id = params.instance_id,
	  title = subj,
	  message = msg,
	  priority = params.priority
	}
	local json = require("dkjson")
	local rc, http_ret, http_code, http_err = request_url(params.api_url .. "/notifications", {
		method = "POST",
		content_type = "application/json",
		postdata = json.encode (tbl, { indent = true }),
		basic_authorization = params.username .. ":" .. params.password
	})
	
	if not rc
	then
		log("HTTP POST to "..params.api_url .. "/systems".." failed with http code ".. http_code .. ". "..http_err ..". Returning "..http_ret, LL_ERROR)
		
		next_check_ms = 30*1000
		next_check_pulseway = next_check_ms
		
		if state.notifications == nil
		then
			state.notifications = {}
		end
		table.insert(state.notifications, {
			subj = subj, msg = msg })
	end		
end

curr_notifications = state.notifications
if curr_notifications ~= nil
then
	state.notifications = {}
	for notification in curr_notifications
	do
		pulseway_notify(notification.subj, notification.msg)
	end
end

--Sends an alert mail if mail address was specified as parameter
function notify_fail(image, passed_time, last_time, alert_time)
	if params.alert_emails == "" and params.username == ""
	then
		return
	end
	
	if alert_time<0
	then
		return
	end

	local btype = "file"
	if image
	then
		btype="image"
		if params.no_images
		then
			return
		end
	elseif params.no_file_backups
	then
		return
	end
	
	local important=" "
	if params.alert_important
	then
		important = "[Important] "
	end
	
	local subj = "[UrBackup]" .. important .. params.clientname .. ": No recent " .. btype .. " backup"
	local lastbackup = "Last " .. btype .." backup was " .. pretty_time(passed_time) .. " ago"
	if last_time==0
	then
		if image
		then
			lastbackup = "Client has never had an image backup"
		else
			lastbackup = "Client has never had a file backup"
		end
	end
	local msg = "No recent " .. btype .. " backup for client \"" .. params.clientname .. "\". ".. lastbackup .. ". This alert is sent if there is no recent backup in the last " .. pretty_time(alert_time) .. "."
	
	if params.alert_emails ~= ""
	then
		mail(params.alert_emails, subj, msg)
	end
	
	if params.username ~= ""
	then
		pulseway_notify(subj, msg)
	end
end

--Sends an ok mail if mail address was specified as parameter, and ok mail was enabled
function notify_ok(image, alert_time)
	if (params.alert_emails == ""  and params.username == "" ) or not params.alert_mail_ok
	then
		return
	end
	
	local btype = "File"
	if image
	then
		btype="Image"
		if params.no_images
		then
			return
		end
	elseif params.no_file_backups
	then
		return
	end
	
	local important=" "
	if params.alert_important
	then
		important = "[Important] "
	end
	
	local subj = "[UrBackup]" .. important .. params.clientname .. ": " .. btype .. " backup status is OK"
	local msg = btype .. " backup status for client \"" .. params.clientname .. "\" is back to ok. Alert was sent because there was no recent backup in the last " .. pretty_time(alert_time) .. "."
	if params.alert_emails ~= ""
	then
		mail(params.alert_emails, subj, msg)
	end
	
	if params.username ~= ""
	then
		pulseway_notify(subj, msg)
	end
end
	
--Time in seconds till file backup status is not ok
local file_backup_nok = file_interval*params.alert_file_mult - params.passed_time_lastbackup_file
if file_backup_nok<0
then
	ret=ret + 1
	
	--Send warning mail only once on status becoming not ok
	if params.file_ok
	then
		notify_fail(false, params.passed_time_lastbackup_file, params.lastbackup_file, file_interval*params.alert_file_mult )
	end
else
	if not params.file_ok
	then
		notify_ok(false, file_interval*params.alert_file_mult)
	end

	next_check_ms = math.min(next_check_ms, file_backup_nok*1000)
end

if string.len(params.os_simple)==0 or params.os_simple=="windows"
then
	--Time in seconds till image backup status is not ok
	local image_backup_nok = image_interval*params.alert_image_mult - params.passed_time_lastbackup_image
	if image_backup_nok<0
	then
		ret= ret + 2
		
		if params.image_ok
		then
			notify_fail(true, params.passed_time_lastbackup_image, params.lastbackup_image, image_interval*params.alert_image_mult )
		end
	else
		if not params.image_ok
		then
			notify_ok(true, image_interval*params.alert_image_mult)
		end
		
		next_check_ms = math.min(next_check_ms, image_backup_nok*1000)
	end
else
	ret = ret + 2
end
	
--Second parameter returns the number of milliseconds to wait till the next status check
--If the interval is complex (not a single number, but different numbers depending on window) we cannot return this time reliably
--The alert script is automatically run after a backup so no need to return the wait time if both image and file backup status is not ok
if params.complex_interval or ret==3
then
	if next_check_pulseway~=nil
	then
		return ret, next_check_pulseway
	else
		return ret
	end
else
	return ret, next_check_ms
end
