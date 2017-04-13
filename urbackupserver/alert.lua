--find largest (maximum of incr and full interval) interval for file and image backups
local file_interval = tonumber(params.incr_file_interval)
local full_file_interval = tonumber(params.full_file_interval)
if full_file_interval>0 and full_file_interval<file_interval
then
	file_interval = full_file_interval
end

local image_interval = tonumber(params.incr_image_interval)
local full_image_interval = tonumber(params.full_image_interval)
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

function pretty_time_append(seconds, name, amount, result)
then
	if seconds>amount
	then
		local t = math.floor(seconds/amount)
		if result~=""
		then
			result = result .. " "
		end
		result = result .. t .. name
		seconds = seconds - t
	end
	
	return seconds, result
end

--Function to pretty print a duration in seconds
function pretty_time(seconds)
then
	result = ""
	seconds, result = pretty_time_append(seconds, " month", 30*60*60*24, result)
	seconds, result = pretty_time_append(seconds, " days", 60*60*24, result)
	seconds, result = pretty_time_append(seconds, "h", 60*60, result)
	seconds, result = pretty_time_append(seconds, "min", 60, result)
	return result
end

--Sends a alert mail if mail address was specified as parameter
function fail_mail(image, passed_time, alert_time)
then
	if params.alert_emails == ""
	then
		return
	end

	local btype = "file"
	if image
	then
		btype="image"
		if params.no_images=="1"
		then
			return
		end
	else if params.no_file_backups=="1"
	then
		return
	end
	
	local subj = "[UrBackup] " .. params.clientname .. ": No recent " .. btype .. " backup"
	local msg = "No recent " .. btype .. " backup for client \"" .. params.clientname .. "\". Last " .. btype .." backup was " .. pretty_time(passed_time) .. " ago. This alert is sent if there is no recent backup in the last " .. pretty_time(alert_time) .. "."
	mail(params.alert_emails, subj, msg)
end

--Time in seconds till file backup status is not ok
local file_backup_nok = file_interval*tonumber(params.alert_file_mult) - tonumber(params.passed_time_lastbackup_file)
if file_backup_nok<0 and file_interval>0
then
	ret=ret + 1
	
	--Send warning mail only once on status becoming not ok
	if params.file_ok=="1"
	then
		fail_mail(false, tonumber(params.passed_time_lastbackup_file), file_interval*tonumber(params.alert_file_mult) )
	end
else
	next_check_ms = math.min(next_check_ms, file_backup_nok*1000)
end

--Time in seconds till image backup status is not ok
local image_backup_nok = image_interval*tonumber(params.alert_image_mult) - tonumber(params.passed_time_lastbackup_image)
if image_backup_nok<0 and image_interval>0
then
	ret= ret + 2
	
	if params.image_ok=="1"
	then
		fail_mail(true, tonumber(params.passed_time_lastbackup_image), file_interval*tonumber(params.alert_image_mult) )
	end
else
	next_check_ms = math.min(next_check_ms, image_backup_nok*1000)
end

--Second parameter returns the number of milliseconds to wait till the next status check
--If the interval is complex (not a single number, but different numbers depending on window) we cannot return this time reliably
--The alert script is automatically run after a backup so no need to return the wait time if both image and file backup status is not ok
if params.complex_interval=="1" or ret==3
then
	return ret
else
	return ret, next_check_ms
end
