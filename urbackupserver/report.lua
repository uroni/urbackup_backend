local subj = "UrBackup: "
local msg = "UrBackup just did "

if params.incremental>0
then
	if params.resumed
	then
		msg = msg .. "a resumed incremental "
		subj = subj .. "Resumed incremental "
	else
		msg = msg .. "an incremental "
		subj = subj .. "Incremental "
	end
else
	if params.resumed
	then
		msg = msg .. "a resumed full "
		subj = subj .. "Resumed full "
	else
		msg = msg .. "a full "
		subj = subj .. "Full "
	end
end

if params.image>0
then
	msg = msg .. "image "
	subj = subj .. "image "
else
	msg = msg .. "file "
	subj = subj .. "file "
end

subj = subj .. "backup of \"" .. params.clientname .. "\"\n"
msg = msg .. "backup of \"" .. params.clientname .. "\".\n"
msg = msg .. "\nReport:\n"
msg = msg .. "( " .. params.infos
if params.infos~=1 then msg = msg .. " infos, "
else msg = msg .. " info, " end
msg = msg .. params.warnings
if params.warnings~=1 then msg = msg .. " warnings, "
else msg = msg .. " warning, " end
msg = msg .. params.errors
if params.errors~=1 then msg = msg .. " errors)\n\n"
else msg = msg .. " error)\n\n" end

for i, v in ipairs(params.data)
do
	local ll = "(info)"
	if v.ll==1 then ll="(warning)"
	elseif v.ll==2 then ll="(error)" end
	msg = msg .. os.date("%Y-%m-%d %H:%M:%S", v.time) .. ll .. ": " .. v.msg .. "\n"
end

if params.success
then
	subj = subj .. " - success"
else
	subj = subj .. " - failed"
end

mail(params.report_mail, subj, msg)

return 0
