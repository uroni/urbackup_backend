from distutils.core import setup
import matplotlib as mpl
import py2exe
import sys

sys.argv.append('py2exe')

opts = {
    'py2exe': {"bundle_files" : 3,
               "includes" : [ "socket", "sysconfig", "matplotlib.backends",  
                            "matplotlib.backends.backend_qt4agg",
                            "pylab", "numpy", 
                            "matplotlib.backends.backend_tkagg"],
                'excludes': ['tcl', 'tcl8.5', 'tk8.5', '_gtkagg', '_tkagg', 
                            '_cairo', '_cocoaagg',
                            '_fltkagg', '_gtk', '_gtkcairo', ],
                'dll_excludes': ['libgdk-win32-2.0-0.dll',
                            'libgobject-2.0-0.dll', 'MSVCP90.dll']
              }
       }

setup(console=[{"script" : "pychart.py"}],data_files=mpl.get_py2exe_datafiles(), 
                            options=opts)
