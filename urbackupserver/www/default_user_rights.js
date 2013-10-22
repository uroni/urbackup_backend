g.defaultUserRights = function(clientid)
{
	return (
	[
		{ domain: "browse_backups", right: clientid },
		{ domain: "lastacts", right: clientid },
		{ domain: "progress", right: clientid },
		{ domain: "settings", right: clientid },
		{ domain: "status", right: clientid },
		{ domain: "logs", right: clientid },
		{ domain: "stop_backup", right: clientid },
		{ domain: "start_backup", right: "all" },
		{ domain: "download_image", right: clientid },
		{ domain: "client_settings", right: clientid }
	]);
}