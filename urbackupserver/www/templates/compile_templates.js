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

function generate_templates()
{
	console.log("Generating templates...");
	
	fs.writeFileSync(templates_file+".new", '');

	files = fs.readdirSync('.');

	files = files.sort(function(a, b) {
		return a===b ? 0 : ( a < b ? -1 : 1);
	});

	files.forEach(function(file)
	{
		if(getExtension(file)=="htm")
		{
			data = fs.readFileSync(file, 'utf8');
				
			if (data.charCodeAt(0) == 65279) {
				data = data.substring(1);
			}
			console.log("Compiling template "+file+" ...");
			fs.appendFileSync(templates_file+".new", dust.compile(data, file.substring(0, file.length-4))+"\n");
		}
	 });

	 console.log("Done compiling templates.");
				
	fs.createReadStream(templates_file+".new").pipe(fs.createWriteStream(templates_file));
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

