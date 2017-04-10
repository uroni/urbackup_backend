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

local ret = 0
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

function pretty_time(seconds)
then
	result = ""
	seconds, result = pretty_time_append(seconds, " month", 30*60*60*24, result)
	seconds, result = pretty_time_append(seconds, " days", 60*60*24, result)
	seconds, result = pretty_time_append(seconds, "h", 60*60, result)
	seconds, result = pretty_time_append(seconds, "min", 60, result)
	return result
end

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
	end
	local subj = "[UrBackup] " .. params.clientname .. ": No recent " .. btype .. " backup"
	local msg = "No recent " .. btype .. " backup for client \"" .. params.clientname .. "\". Last " .. btype .." backup was " .. pretty_time(passed_time) .. " ago. This alert is sent if there is no recent backup in the last " .. pretty_time(alert_time) .. "."
	mail(params.alert_emails, subj, msg)
end

local file_backup_nok = file_interval*tonumber(params.alert_file_mult) - tonumber(params.passed_time_lastbackup_file)
if file_backup_nok<0 and file_interval>0
then
	ret=ret + 1
	
	if params.file_ok=="1"
	then
		fail_mail(false, tonumber(params.passed_time_lastbackup_file), file_interval*tonumber(params.alert_file_mult) )
	end
else
	next_check_ms = math.min(next_check_ms, file_backup_nok)
end

local image_backup_nok = image_interval*tonumber(params.alert_image_mult) - tonumber(params.passed_time_lastbackup_image)
if image_backup_nok<0 and image_interval>0
then
	ret= ret + 2
	
	if params.image_ok=="1"
	then
		fail_mail(true, tonumber(params.passed_time_lastbackup_image), file_interval*tonumber(params.alert_image_mult) )
	end
else
	next_check_ms = math.min(next_check_ms, image_backup_nok)
end

if params.complex_interval=="1" or ret==3
then
	return ret
else
	return ret, next_check_ms
end
