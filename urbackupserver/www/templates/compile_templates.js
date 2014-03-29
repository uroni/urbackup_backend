var fs = require('fs');
var path = require('path');
var dust = require("dustjs-linkedin");


function getExtension(filename) {
    var ext = path.extname(filename||'').split('.');
    return ext[ext.length - 1];
}

var templates_file="../templates.js";

fs.writeFileSync(templates_file, '');

fs.readdir('.', function(err,files){
    if(err) throw err;
	files.sort(function(a, b) {
        return a < b ? -1 : 1;
    }).forEach(function(file){
		if(getExtension(file)=="htm")
		{
			fs.readFile(file, 'utf8', function (err,data) {
				if (err) throw err;
				if (data.charCodeAt(0) == 65279) {
                    data = data.substring(1);
                }
				console.log("Compiling template "+file+" ...");
				fs.appendFileSync(templates_file, dust.compile(data, file.substring(0, file.length-4))+"\n");
			});
		}
    });
 });
 
 console.log("Done compiling templates.");