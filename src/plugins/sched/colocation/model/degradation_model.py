# -*- coding: utf-8 -*-
#Predicting the degradation through ML
def colocation_pairs2(queue):

	import sys
	import pickle

	sys.path.insert(0, 'graph/')
	#Importing graph structure
	from grafo import Grafo
	from blossom_min import *

	grafo = Grafo()
	schedule = []
	apps_counters = {}
	joblist = []


	#creating dictionary for building degradation graph
	for job in queue:
		jobid = job[0]
		apps_counters[jobid] = job[1]
		joblist.append(jobid)

	#Load machine learning model
	#loaded_model = pickle.load(open("linear_regression.sav", 'rb'))
	loaded_model = pickle.load(open("mlpregressor.sav", 'rb'))
	#Load scaling used on training fase
	scaling_model = pickle.load(open("scaling.sav", 'rb'))

	#For each job create degradation graph
	for jobMain in joblist:
		for jobSecond in joblist:
			if(jobMain != jobSecond):
				prediction = apps_counters[jobMain] + apps_counters[jobSecond]
				prediction_normalized = scaling_model.transform([prediction])
				degradationMain = loaded_model.predict(prediction_normalized)

				prediction = apps_counters[jobSecond] + apps_counters[jobMain]
				prediction_normalized = scaling_model.transform([prediction])
				degradationSecond = loaded_model.predict(prediction_normalized)
						
				#print("[{r1} {r2}]{r3} {r4}".format(r1=jobMain, r2=jobSecond, r3=degradationMain, r4=degradationSecond))	
				#grafo.add_aresta(str(jobMain), str(jobSecond), max(degradationMain, degradationSecond))
				grafo.add_aresta(jobMain, jobSecond, max(degradationMain[0], degradationSecond[0]))


	#Apply minimum weight perfect matching to find optimal co-schedule
	blossom = min_weight_matching(grafo)


	#Creating Output
	joblist_pairs = blossom.keys()
	for key in joblist_pairs:
		jobid1 = key
		jobid2 = blossom[key][0]
		degradation = blossom[key][1]
		schedule.append((jobid1, jobid2, degradation))

	return schedule

def colocation_pairs(a,b):
	return a+b

#lista = [(100,[1.19796833773087,0.238333808524628,9.23218997361478,4.91665113861468,1501199.31398417,1489795.36745549,13241816.1398417,7976065.99991967,295.997361477573,0.632031580898326,0.000262808815693145,0.000455307280223879,0.00190850490239199,0.00210457431042719]),
#         (110,[0.572118811881188,0.00508516190513594,10,0,25448882.5188119,914046.608718328,242734893.304951,8682420.69484401,397,0,0.0034950700740341,3.15013362270567e-05,0.0333360270739527,0.000243981899186293]),
#         (120,[0.740936708860759,0.00291739824007024,5.75189873417721,0.438288306329748,1708297.56962025,177983.208917073,28344051.9594937,2978755.43477004,398,0,0.00018122390488643,2.80033330753662e-06,0.00300277144158753,2.71349693602236e-05]),
#         (140,[0.147565217391304,0.0503019928824276,48.9565217391304,1.95535901995139,86101142.4565217,9246629.60038751,173157401.895652,15596296.0678312,387,0,0.0473182005465378,0.00560749465649807,0.095035969467566,0.0101748527377586]),
#         (150,[1.41615720524017,0.0145726162041088,12.3013100436681,0.772923383628731,9997843.68995633,2093120.74363845,78355183.3275109,17394141.4276727,366.672489082969,47.4398250733265,0.000613736618348111,4.41829844536788e-05,0.00478258140822988,0.000116104485397998]),
#         (160,[1.54502272727273,0.018348671395378,77.6954545454546,14.4996418861669,13037213.9613636,2586046.57917585,16604787.7204545,1370979.91533673,396,0,0.000669321559609446,0.000121860710065397,0.000857289645552235,4.44671662063561e-06]),
#         (180,[0.829179104477612,0.0749368662700682,0.0870646766169154,0.594976342953511,675790.425373134,779979.923015978,135202229.940298,47419967.0676752,336.39552238806,1.20484468871985,7.57016841122552e-05,0.000104962390660067,0.0152134428222276,0.00101816269114499]),
#         (190,[0.408155172413793,0.233534551905015,51.6379310344828,3.27600938768327,381741718.386207,69824573.4375761,722329049.87069,129470196.932967,397,0,0.0817017213966283,0.0151262236985286,0.154510719911071,0.0284466280335838])]


#ml = colocation_pairs(lista)
#print("Model schedule = {r1}".format(r1=ml))

