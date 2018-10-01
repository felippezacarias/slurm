# -*- coding: utf-8 -*-
#Predicting the degradation through ML
#optimal
def colocation_pairs_optimal(queue, degradation_limit):
#def colocation_pairs(queue, degradation_limit):

	try:
		import sys
		import time
			
		import pickle

		start_tot = time.time()

		#Depois colocar esse "/opt/slurm/lib/degradation_model/" pra 
		#ser pego da variável de ambiente
		sys.path.insert(0, '/opt/slurm_teste_final/lib/degradation_model/graph/')
		#Arquivo apenas necessário para propósitos de debug
		#with open('/tmp/PYTHON_PATH.txt', 'a') as f:
		#	print >> f, 'Filename:', sys.path  # Python 2.x
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
		loaded_model = pickle.load(open("/opt/slurm_teste_final/lib/degradation_model/mlpregressor.sav", 'rb'))
		#loaded_model = pickle.load(open("/opt/slurm_teste_final/lib/degradation_model/random_forest.sav", 'rb'))
		#Load scaling used on training fase
		scaling_model = pickle.load(open("/opt/slurm_teste_final/lib/degradation_model/scaling.sav", 'rb'))

		time_predict = 0.0
		time_graph = 0.0
		time_append = 0.0
		time_scaling = 0.0
		count = 0


		for it in range(0,len(queue),1):
			for j in range(it+1,len(queue),1):
				count += 1
				#print("job1 = {r1} job2 = {r2}".format(r1=joblist[it],r2=joblist[j]))
				start = time.time() 
				prediction = apps_counters[joblist[it]] + apps_counters[joblist[j]]
				time_append += (time.time() - start)
				start = time.time()
				prediction_normalized = scaling_model.transform([prediction])
				time_scaling += (time.time() - start)
				start = time.time()
				degradationMain = loaded_model.predict(prediction_normalized)
				time_predict += (time.time() - start)

				start = time.time()
				prediction = apps_counters[joblist[j]] + apps_counters[joblist[it]]
                                time_append += (time.time() - start)
                                start = time.time()
				prediction_normalized = scaling_model.transform([prediction])
				time_scaling += (time.time() - start)
				start = time.time()
				degradationSecond = loaded_model.predict(prediction_normalized)
				time_predict += (time.time() - start)

				start = time.time()
				grafo.add_aresta(joblist[it], joblist[j], max(degradationMain[0], degradationSecond[0]))
				time_graph += (time.time() - start)

				

		#For each job create degradation graph
		#for jobMain in joblist:
		#	for jobSecond in joblist:
		#		if(jobMain != jobSecond):
		#			count += 1
		#			start = time.time()
		#			prediction = apps_counters[jobMain] + apps_counters[jobSecond]
		#			time_append += (time.time() - start)
		#			start = time.time()
		#			prediction_normalized = scaling_model.transform([prediction])
		#			time_scaling += (time.time() - start)
		#			start = time.time()
		#			degradationMain = loaded_model.predict(prediction_normalized)
		#			time_predict += (time.time() - start)

		#			start = time.time()
		#			prediction = apps_counters[jobSecond] + apps_counters[jobMain]
		#			time_append += (time.time() - start)
		#			start = time.time()
		#			prediction_normalized = scaling_model.transform([prediction])
		#			time_scaling += (time.time() - start)
		#			start = time.time()
		#			degradationSecond = loaded_model.predict(prediction_normalized)
		#			time_predict += (time.time() - start)

					#print("[{r1} {r2}]{r3} {r4}".format(r1=jobMain, r2=jobSecond, r3=degradationMain, r4=degradationSecond))	
					#grafo.add_aresta(str(jobMain), str(jobSecond), max(degradationMain, degradationSecond))
		#			start = time.time()
		#			grafo.add_aresta(jobMain, jobSecond, max(degradationMain[0], degradationSecond[0]))
		#			time_graph += (time.time() - start)


		#Apply minimum weight perfect matching to find optimal co-schedule
		start = time.time()
		blossom = min_weight_matching(grafo)
		time_blossom = time.time() - start

		#Creating Output
		schedule_s = []
		start = time.time()
		joblist_pairs = blossom.keys()
		with open('/tmp/SLURM_PYTHON_SCHEDULE_DEBUG.txt', 'a') as f:
			#print >> f, 'EXECUTION: ', len(queue)  # Python 2.x
			#print >> f, 'Entrou: ', count
			#print >> f, 'PAIRS CREATED: ', blossom  # Python 2.x
			for key in joblist_pairs:
				jobid1 = key
				jobid2 = blossom[key][0]
				degradation = blossom[key][1]
				if(degradation > degradation_limit):
						schedule.append([jobid1])
						schedule.append([jobid2])
				else:
						schedule.append(sorted((jobid1, jobid2)))

			time_tresh = time.time() - start
			start = time.time()
			schedule_s = sorted(schedule, key=lambda tup: tup[0])
			time_sort = time.time() - start
			#print >> f, 'FINAL RESULT: ', schedule_s  # Python 2.x
			print >> f, 'queue_len ', len(queue), 'time_tot ', (time.time() - start_tot ),'time_append ',time_append,'time_scaling ',time_scaling, 'time_predict ',time_predict,'time_graph ',time_graph, 'time_blossom ',time_blossom ,'time_tresh ',time_tresh ,'time_sort ',time_sort # Python 2.x
			#print >> f, 'FINAL RESULT: ', schedule_s  # Python 2.x
		f.closed

		return schedule_s

	except Exception, e:
		with open('/tmp/SLURM_PYTHON_ERROR.txt', 'a') as f:
			print >> f, 'Filename:', type(e)  # Python 2.x
			print >> f, 'Filename:', str(e)  # Python 2.x
			print >> f, 'queue:', queue  # Python 2.x
			print >> f, 'Filename:', type(queue)  # Python 2.x
			for job in queue:
				print >> f, 'type job[0]',type(job[0])
				print >> f, 'type job[1]',type(job[1])				
				print >> f, '++++++++++++++++++++++++:'  # Python 2.x
			print >> f, 'degradation_limit:', degradation_limit  # Python 2.x
			print >> f, 'Filename:', type(degradation_limit)  # Python 2.x
			#print('Filename:', str(e), file=f)  # Python 3.x
		f.closed

#Greedy approach
def colocation_pairs_greedy(queue, degradation_limit):
#def colocation_pairs(queue, degradation_limit):

	try:
		import sys
		import time
	
		import pickle

		start_tot = time.time()

		#Depois colocar esse "/opt/slurm/lib/degradation_model/" pra 
		#ser pego da variável de ambiente
		sys.path.insert(0, '/opt/slurm_teste_final/lib/degradation_model/graph/')
		#Arquivo apenas necessário para propósitos de debug
		#with open('/tmp/PYTHON_PATH.txt', 'a') as f:
		#	print >> f, 'Filename:', sys.path  # Python 2.x


		schedule = []
		apps_counters = {}
		joblist = []
		greedy_list = []
		list_jobid = []
		schedule_s = []


		#creating dictionary for building degradation graph
		for job in queue:
			jobid = job[0]
			apps_counters[jobid] = job[1]
			joblist.append(jobid)

		#Load machine learning model
		#loaded_model = pickle.load(open("/opt/slurm_teste_final/lib/degradation_model/random_forest.sav", 'rb'))
		loaded_model = pickle.load(open("/opt/slurm_teste_final/lib/degradation_model/mlpregressor.sav", 'rb'))
		#Load scaling used on training fase
		scaling_model = pickle.load(open("/opt/slurm_teste_final/lib/degradation_model/scaling.sav", 'rb'))

		time_predict = 0.0
		time_graph = 0.0
		time_greedy_list = 0.0
		time_append = 0.0
		time_scaling = 0.0
		contador = 0.0

		for it in range(0,len(queue),1):
			for j in range(it+1,len(queue),1):
				contador = contador + 1.0
				#print("job1 = {r1} job2 = {r2}".format(r1=joblist[it],r2=joblist[j]))
				start = time.time() 
				prediction = apps_counters[joblist[it]] + apps_counters[joblist[j]]
				time_append += (time.time() - start)
				start = time.time()
				prediction_normalized = scaling_model.transform([prediction])
				time_scaling += (time.time() - start)
				start = time.time()
				degradationMain = loaded_model.predict(prediction_normalized)
				time_predict += (time.time() - start)

				start = time.time()
				prediction = apps_counters[joblist[j]] + apps_counters[joblist[it]]
                                time_append += (time.time() - start)
                                start = time.time()
				prediction_normalized = scaling_model.transform([prediction])
				time_scaling += (time.time() - start)
				start = time.time()
				degradationSecond = loaded_model.predict(prediction_normalized)
				time_predict += (time.time() - start)
				
				start = time.time()
				greedy_list.append((joblist[it], joblist[j], max(degradationMain[0], degradationSecond[0])))
				time_greedy_list += (time.time() - start)


		start = time.time()
		greedy_list = sorted(greedy_list, key=lambda tup: tup[2])
		for tup in greedy_list:
			if not (set([tup[0]]).issubset(list_jobid) or set([tup[1]]).issubset(list_jobid)):
				schedule.append(tup)
				list_jobid.append(tup[0])
				list_jobid.append(tup[1])
		time_greedy_sort = time.time() - start

		with open('/tmp/SLURM_PYTHON_SCHEDULE_GREEDY_DEBUG.txt', 'a') as f:
			#print >> f, 'EXECUTION: ',len(queue)  # Python 2.x
			#print >> f, 'Entrou: ', contador
			#print >> f, 'GREEDY_LIST: ', greedy_list
			#print >> f, 'PAIRS CREATED: ', schedule  # Python 2.x
			start = time.time()
			for tup in schedule:
				degradation = tup[2]
				if(degradation > degradation_limit):
						schedule_s.append([tup[0]])
						schedule_s.append([tup[1]])
				else:
						schedule_s.append(sorted((tup[0], tup[1])))

			time_tresh = time.time() - start
			start = time.time()
			schedule_s = sorted(schedule_s, key=lambda tup: tup[0])
			time_sort = time.time() - start
			#print >> f, 'FINAL SCHEDULE: ', schedule_s  # Python 2.x
			print >> f,'queue_len ',len(queue), 'time_tot ', (time.time() - start_tot ),'time_append ',time_append,'time_scaling ',time_scaling, 'time_predict ',time_predict,'time_greedy_list ',time_greedy_list, 'time_greedy_sort ',time_greedy_sort ,'time_tresh ',time_tresh ,'time_sort ',time_sort # Python 2.x

		return schedule_s

	except Exception, e:
		with open('/tmp/SLURM_PYTHON_GREEDY_ERROR.txt', 'a') as f:
			print >> f, 'Filename:', type(e)  # Python 2.x
			print >> f, 'Filename:', str(e)  # Python 2.x
			print >> f, 'Filename:', type(queue)  # Python 2.x
			print >> f, 'degradation_limit:', degradation_limit  # Python 2.x
			print >> f, 'Filename:', type(degradation_limit)  # Python 2.x
			#print('Filename:', str(e), file=f)  # Python 3.x
		f.closed

def colocation_pairs2(queue, degradation_limit):
	tupla1 = queue[0]
	tupla2 = queue[1]
 	return [[tupla1[0],tupla2[0]]]

#def colocation_pairs_dio(queue, degradation_limit):
def colocation_pairs(queue, degradation_limit):
	try:    
		import time    
		start = time.time()
		qList = sorted(queue, key=lambda tup: tup[1])
        	sched = []
	        for i in range(0,len(qList)/2,1):
        	        f = len(qList)-1
                	sched.append([qList[i][0],qList[f-i][0]])
	        if (len(qList)%2): sched.append([qList[len(qList)/2][0]])
		end = time.time()        

		with open('/tmp/SLURM_DIO_DEBUG.txt', 'a') as f:
			print >> f,'FINAL SCHEDULE: ', sched	
			print >> f,'queue_len ',len(queue), 'time_tot ', (end - start) # Python 2.x

		return sched
	
	except Exception, e:
                with open('/tmp/SLURM_DIO_ERROR.txt', 'a') as f:
                        print >> f, 'Filename:', type(e)  # Python 2.x
                        print >> f, 'Filename:', str(e)  # Python 2.x
                        print >> f, 'Filename:', type(queue)  # Python 2.x
                        print >> f, 'degradation_limit:', degradation_limit  # Python 2.x
                f.closed

#lista = [(100,[1.19796833773087,0.238333808524628,9.23218997361478,4.91665113861468,1501199.31398417,1489795.36745549,13241816.1398417,7976065.99991967,295.997361477573,0.632031580898326,0.000262808815693145,0.000455307280223879,0.00190850490239199,0.00210457431042719]),
#         (110,[0.572118811881188,0.00508516190513594,10,0,25448882.5188119,914046.608718328,242734893.304951,8682420.69484401,397,0,0.0034950700740341,3.15013362270567e-05,0.0333360270739527,0.000243981899186293]),
#         (120,[0.740936708860759,0.00291739824007024,5.75189873417721,0.438288306329748,1708297.56962025,177983.208917073,28344051.9594937,2978755.43477004,398,0,0.00018122390488643,2.80033330753662e-06,0.00300277144158753,2.71349693602236e-05]),
#         (140,[0.147565217391304,0.0503019928824276,48.9565217391304,1.95535901995139,86101142.4565217,9246629.60038751,173157401.895652,15596296.0678312,387,0,0.0473182005465378,0.00560749465649807,0.095035969467566,0.0101748527377586]),
#         (150,[1.41615720524017,0.0145726162041088,12.3013100436681,0.772923383628731,9997843.68995633,2093120.74363845,78355183.3275109,17394141.4276727,366.672489082969,47.4398250733265,0.000613736618348111,4.41829844536788e-05,0.00478258140822988,0.000116104485397998]),
#         (160,[1.54502272727273,0.018348671395378,77.6954545454546,14.4996418861669,13037213.9613636,2586046.57917585,16604787.7204545,1370979.91533673,396,0,0.000669321559609446,0.000121860710065397,0.000857289645552235,4.44671662063561e-06]),
#         (180,[0.829179104477612,0.0749368662700682,0.0870646766169154,0.594976342953511,675790.425373134,779979.923015978,135202229.940298,47419967.0676752,336.39552238806,1.20484468871985,7.57016841122552e-05,0.000104962390660067,0.0152134428222276,0.00101816269114499]),
#         (190,[0.408155172413793,0.233534551905015,51.6379310344828,3.27600938768327,381741718.386207,69824573.4375761,722329049.87069,129470196.932967,397,0,0.0817017213966283,0.0151262236985286,0.154510719911071,0.0284466280335838])]


#ml = colocation_pairs(lista,100)
#print("Model schedule = {r1}".format(r1=ml))
#RESULT: [[jobid,jobid],[jobid]...] Model schedule = [[100, 110], [120], [140], [150, 190], [160], [180]]
