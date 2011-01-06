import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from numpy import *


g_dpi=80

def drawGraph(tfn, dim, data, xlabel, ylabel, title, style, pltformat, sizex, sizey):
	global g_dpi
	
	if dim==2:
		x = data[0]
		y = data[1]
		plt.xlabel(xlabel)
		plt.ylabel(ylabel)
		plt.title(title)
		fig=plt.figure(figsize=(sizex/g_dpi,sizey/g_dpi), dpi=g_dpi)
		plt.plot(x,y, style, figure=fig )
		plt.savefig(tfn, transparent=True, format=pltformat, dpi=g_dpi)
		plt.close(fig)
		
		
def drawPie(tfn, data, title, pLabels, pColors, pShadow, pltformat, sizex, sizey):
	global g_dpi
	
	if len(data)!=len(pLabels):
		pLabels=None
	
	if len(pColors)==0:
		pColors=None
		
	plt.title(title)
	fig=plt.figure(figsize=(sizex/g_dpi,sizey/g_dpi), dpi=g_dpi)	
	plt.pie(data, labels=pLabels, colors=pColors, shadow=pShadow)
	plt.savefig(tfn, transparent=True, format=pltformat, dpi=g_dpi)
	plt.close(fig)
	
	
def drawBar(tfn, data, xlabels, ylabel, title, barcolor, pltformat, sizex, sizey, width):
	global g_dpi
	
	if len(barcolor)==0:
		barcolor=None
	
	x = data
	fig=plt.figure(figsize=(sizex/g_dpi,sizey/g_dpi), dpi=g_dpi)
	plt.bar(arange(len(data)), x, width, color=barcolor, figure=fig )
	plt.xticks(arange(len(data))+width/2.,xlabels)
	plt.ylabel(ylabel)
	plt.title(title)
	plt.savefig(tfn, transparent=True, format=pltformat, dpi=g_dpi)
	plt.close(fig)