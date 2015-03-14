var fs = require('fs');
var path = require('path');
var dust = require("dustjs-linkedin");
var EventEmitter=require('events').EventEmitter,
    filesEE=new EventEmitter();


function getExtension(filename) {
    var ext = path.extname(filename||'').split('.');
    return ext[ext.length - 1];
}

var templates_file="../js/templates.js";
var generating = false;

function generate_templates()
{
	if(generating==true)
	{
		setTimeout(generate_templates, 100);
		return;
	}
	generating = true;
	console.log("Generating templates...");
	
	fs.writeFileSync(templates_file, '');
	
	var open_files = 0;

	fs.readdir('.', function(err,files){
		if(err) throw err;
		files.sort(function(a, b) {
			return a===b ? 0 : ( a < b ? -1 : 1);
		}).forEach(function(file){
			if(getExtension(file)=="htm")
			{
				++open_files;
				fs.readFile(file, 'utf8', function (err,data) {
					if (err) throw err;
					if (data.charCodeAt(0) == 65279) {
						data = data.substring(1);
					}
					console.log("Compiling template "+file+" ...");
					fs.appendFileSync(templates_file, dust.compile(data, file.substring(0, file.length-4))+"\n");
					--open_files;
					
					if(open_files==0)
					{
						console.log("Done compiling templates.");
						generating = false;
					}					
				});
			}
		});
	 });	 
}

generate_templates();

if(process.argv.length>2 && process.argv[2]=="watch")
{
	console.log("Watching for directory changes.");
	var chokidar = require('chokidar');
	var watcher = chokidar.watch('.', {persistent: true, ignoreInitial: true});
	watcher
	  .on('add', generate_templates)
	  .on('change', generate_templates)
	  .on('unlink', generate_templates)
	  .on('error', function(error) {console.error('Error happened', error);})
}

