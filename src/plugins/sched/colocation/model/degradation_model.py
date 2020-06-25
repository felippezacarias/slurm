# -*- coding: utf-8 -*-
#Predicting the degradation through ML
#using the blossom algorithm to compute colocation_pairs
def optimal(queue, degradation_limit, model_name):

	try:
		import sys
		from os import path
		import pickle
		import time

		#Importing graph structure. Path should be in $PYTHONPATH
		from grafo import Grafo
		from blossom_min import *

		start_tot = time.time()

		grafo = Grafo()
		schedule = []
		apps_counters = {}
		time_alone = {}
		joblist = []

		time_predict = 0.0
		time_graph = 0.0


		#Dealing with odd queue
		if len(queue) % 2:
			del queue[-1]

		#creating dictionary for building degradation graph
		for job in queue:
			jobid = job[0]
			l = job[1]
			time_alone[jobid] = l.pop(0)
			apps_counters[jobid] = l
			joblist.append(jobid)

		#Load machine learning model
		basepath = path.dirname(__file__)
		loaded_model = pickle.load(open(path.join(basepath, model_name), 'rb'))
		#Load scaling used on training fase
		scaling_model = pickle.load(open(path.join(basepath,"scaling.sav"), 'rb'))

		#For each job create degradation graph
		for jobMain in joblist:
			for jobSecond in joblist:
				if(jobMain != jobSecond):
					prediction = apps_counters[jobMain] + apps_counters[jobSecond]
					prediction_normalized = scaling_model.transform([prediction])
					start = time.time()
					degradationMain = loaded_model.predict(prediction_normalized)
					time_predict += (time.time() - start)

					prediction = apps_counters[jobSecond] + apps_counters[jobMain]
					prediction_normalized = scaling_model.transform([prediction])
					start = time.time()
					degradationSecond = loaded_model.predict(prediction_normalized)
					time_predict += (time.time() - start)

					t1 = time_alone[jobMain] + (time_alone[jobMain]*(degradationMain[0]/100))
					t2 = time_alone[jobSecond] + (time_alone[jobSecond]*(degradationSecond[0]/100))


					grafo.add_aresta(jobMain, jobSecond, int(max(t1,t2)))


		#Apply minimum weight perfect matching to find optimal co-schedule
		start = time.time()
		blossom = min_weight_matching(grafo)
		time_blossom = time.time() - start


		#Creating Output
		schedule_s = []
		start = time.time()
		joblist_pairs = blossom.keys()
		with open('/tmp/SLURM_PYTHON_SCHEDULE_DEBUG.txt', 'a') as f:
			print >> f, 'EXECUTION:'  # Python 2.x
			print >> f, 'OPTIMAL RESULT: ', blossom  # Python 2.x
			for key in joblist_pairs:
				jobid1 = key
				jobid2 = blossom[key][0]
				makespan = blossom[key][1]
				fifo = time_alone[jobid1]+time_alone[jobid2]
				if(makespan > fifo):
						schedule.append([jobid1])
						schedule.append([jobid2])
				else:
						schedule.append(sorted((jobid1, jobid2)))

			schedule_s = sorted(schedule, key=lambda tup: tup[0])
			time_tresh = time.time() - start
			print >> f, 'FINAL RESULT: ', schedule_s  # Python 2.x
			print >> f, 'queue_len ', len(queue), 'time_tot ', (time.time() - start_tot ),'time_predict ',time_predict,'time_blossom ',time_blossom ,'time_tresh ',time_tresh
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
def greedy(queue, degradation_limit, model_name):

	try:
		import sys
		from os import path
		import pickle
		import time

		start_tot = time.time()

		schedule = []
		apps_counters = {}
		time_alone = {}
		joblist = []
		greedy_list = []
		list_jobid = []
		schedule_s = []

		#creating dictionary for building degradation graph
		for job in queue:
			jobid = job[0]
			l = job[1]
			time_alone[jobid] = l.pop(0)
			apps_counters[jobid] = l
			joblist.append(jobid)

		#Load machine learning model
		basepath = path.dirname(__file__)
		loaded_model = pickle.load(open(path.join(basepath, model_name), 'rb'))
		#Load scaling used on training fase
		scaling_model = pickle.load(open(path.join(basepath,"scaling.sav"), 'rb'))

		time_predict = 0.0

		for it in range(0,len(queue),1):
			for j in range(it+1,len(queue),1):
				prediction = apps_counters[joblist[it]] + apps_counters[joblist[j]]
				prediction_normalized = scaling_model.transform([prediction])

				start = time.time() 
				degradationMain = loaded_model.predict(prediction_normalized)
				time_predict += (time.time() - start)
				prediction = apps_counters[joblist[j]] + apps_counters[joblist[it]]
				prediction_normalized = scaling_model.transform([prediction])
				start = time.time() 
				degradationSecond = loaded_model.predict(prediction_normalized)
				time_predict += (time.time() - start)

				t1 = time_alone[joblist[it]] + (time_alone[joblist[it]]*(degradationMain[0]/100))
				t2 = time_alone[joblist[j]] + (time_alone[joblist[j]]*(degradationSecond[0]/100))
					
				greedy_list.append((joblist[it], joblist[j], int(max(t1, t2))))


		start = time.time()
		greedy_list = sorted(greedy_list, key=lambda tup: tup[2])
		for tup in greedy_list:
			if not (set([tup[0]]).issubset(list_jobid) or set([tup[1]]).issubset(list_jobid)):
				schedule.append(tup)
				list_jobid.append(tup[0])
				list_jobid.append(tup[1])	
		time_greedy_sort = time.time() - start


		with open('/tmp/SLURM_PYTHON_SCHEDULE_GREEDY_DEBUG.txt', 'a') as f:
			print >> f, 'EXECUTION:'  # Python 2.x
			print >> f, 'GREEDY_LIST: ', greedy_list
			print >> f, 'PAIRS CREATED: ', schedule  # Python 2.x

			start = time.time()			
			res_dic = {}
			#Example of expected output: 
			#[(100, [110, 200, 210]), (110, [100, 200, 210]), (120, []), (200, [100, 110, 210]), (210, [100, 110, 200]), (220, [])]
			for tup in greedy_list:
				if not tup[0] in res_dic:
					res_dic[tup[0]] = []
				if not tup[1] in res_dic:
					res_dic[tup[1]] = []
				fifo = time_alone[tup[0]]+time_alone[tup[1]]
				if(tup[2] < fifo):
					res_dic[tup[1]].append(tup[0])
					res_dic[tup[0]].append(tup[1])
			for key in res_dic.keys():
				list_mate = res_dic[key]
				schedule_s.append([key] + list_mate)

			schedule_s = sorted(schedule_s, key=lambda list_: list_[0])
			time_tresh = time.time() - start
			print >> f, 'FINAL SCHEDULE: ', schedule_s  # Python 2.x
			print >> f,'queue_len ',len(queue), 'time_tot ', (time.time() - start_tot ),'time_predict ',time_predict, 'time_greedy_sort ',time_greedy_sort ,'time_tresh ',time_tresh 

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


def optimald(queue, degradation_limit, model_name):

	try:
		import sys
		from os import path
		import pickle

		#Importing graph structure. Path should be in $PYTHONPATH
		from grafo import Grafo
		from blossom_min import *

		grafo = Grafo()
		schedule = []
		apps_counters = {}
		joblist = []

		#Dealing with odd queue
		if len(queue) % 2:
			del queue[-1]

		#creating dictionary for building degradation graph
		for job in queue:
			jobid = job[0]
			apps_counters[jobid] = job[1]
			joblist.append(jobid)

		#Load machine learning model
		basepath = path.dirname(__file__)
		loaded_model = pickle.load(open(path.join(basepath, model_name), 'rb'))
		#Load scaling used on training fase
		scaling_model = pickle.load(open(path.join(basepath,"scaling.sav"), 'rb'))

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
							
					grafo.add_aresta(jobMain, jobSecond, max(degradationMain[0], degradationSecond[0]))


		#Apply minimum weight perfect matching to find optimal co-schedule
		blossom = min_weight_matching(grafo)


		#Creating Output
		schedule_s = []
		joblist_pairs = blossom.keys()
		with open('/tmp/SLURM_PYTHON_SCHEDULE_DEBUG.txt', 'a') as f:
			print >> f, 'EXECUTION:'  # Python 2.x
			print >> f, 'OPTIMAL RESULT: ', blossom  # Python 2.x
			for key in joblist_pairs:
				jobid1 = key
				jobid2 = blossom[key][0]
				degradation = blossom[key][1]
				if(degradation > degradation_limit):
						schedule.append([jobid1])
						schedule.append([jobid2])
				else:
						schedule.append(sorted((jobid1, jobid2)))

			schedule_s = sorted(schedule, key=lambda tup: tup[0])
			print >> f, 'FINAL RESULT: ', schedule_s  # Python 2.x
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
def greedyd(queue, degradation_limit, model_name):

	try:
		import sys
		from os import path
		import pickle

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
		basepath = path.dirname(__file__)
		loaded_model = pickle.load(open(path.join(basepath, model_name), 'rb'))
		#Load scaling used on training fase
		scaling_model = pickle.load(open(path.join(basepath,"scaling.sav"), 'rb'))

		for it in range(0,len(queue),1):
			for j in range(it+1,len(queue),1):
				prediction = apps_counters[joblist[it]] + apps_counters[joblist[j]]
				prediction_normalized = scaling_model.transform([prediction])

				degradationMain = loaded_model.predict(prediction_normalized)
				prediction = apps_counters[joblist[j]] + apps_counters[joblist[it]]
				prediction_normalized = scaling_model.transform([prediction])
				degradationSecond = loaded_model.predict(prediction_normalized)
					
				greedy_list.append((joblist[it], joblist[j], max(degradationMain[0], degradationSecond[0])))


		greedy_list = sorted(greedy_list, key=lambda tup: tup[2])
		for tup in greedy_list:
			if not (set([tup[0]]).issubset(list_jobid) or set([tup[1]]).issubset(list_jobid)):
				schedule.append(tup)
				list_jobid.append(tup[0])
				list_jobid.append(tup[1])	

		with open('/tmp/SLURM_PYTHON_SCHEDULE_GREEDY_DEBUG.txt', 'a') as f:
			print >> f, 'EXECUTION:'  # Python 2.x
			print >> f, 'GREEDY_LIST: ', greedy_list
			print >> f, 'PAIRS CREATED: ', schedule  # Python 2.x
			
			res_dic = {}
			#Example of expected output: 
			#[(100, [110, 200, 210]), (110, [100, 200, 210]), (120, []), (200, [100, 110, 210]), (210, [100, 110, 200]), (220, [])]
			for tup in greedy_list:
				if not tup[0] in res_dic:
					res_dic[tup[0]] = []
				if not tup[1] in res_dic:
					res_dic[tup[1]] = []
				if(tup[2] < degradation_limit):
					res_dic[tup[1]].append(tup[0])
					res_dic[tup[0]].append(tup[1])
			for key in res_dic.keys():
				list_mate = res_dic[key]
				schedule_s.append([key] + list_mate)


			schedule_s = sorted(schedule_s, key=lambda list_: list_[0])
			print >> f, 'FINAL SCHEDULE: ', schedule_s  # Python 2.x

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

def graph_coloring(queue, degradation_limit, model_name):

	try:
		import sys
		from os import path			
		import pickle

		#Importing graph structure
		from grafo import Grafo

		graph = Grafo()
		schedule = []
		apps_counters = {}
		joblist = []


		#creating dictionary for building degradation graph
		for job in queue:
			jobid = job[0]
			apps_counters[jobid] = job[1]
			joblist.append(jobid)

		#Load machine learning model
		basepath = path.dirname(__file__)
		loaded_model = pickle.load(open(path.join(basepath, model_name), 'rb'))
		#Load scaling used on training fase
		scaling_model = pickle.load(open(path.join(basepath,"scaling.sav"), 'rb'))

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
							
					if max(degradationMain[0], degradationSecond[0]) > degradation_limit:
						graph.add_aresta(jobMain, jobSecond, max(degradationMain[0], degradationSecond[0]))


		#Apply the coloring graph to get the result
		result = coloring(graph)
		schedule_s = []

		with open('/tmp/SLURM_PYTHON_SCHEDULE_DEBUG.txt', 'a') as f:
			print >> f, 'EXECUTION:'  # Python 2.x
			print >> f, 'COLORING RESULT: ', result
			
			color = {}
			res_dic = {}
			for jobid in result.keys():
				if not result[jobid] in color:
					color[result[jobid]] = []
				color[result[jobid]].append(jobid)
			for cr in color.keys():
				for job in color[cr]:
					new_list = list(color[cr])
					new_list.remove(job)
					schedule_s.append([job] + new_list)
				
			schedule_s = sorted(schedule_s, key=lambda list_: list_[0])
			print >> f, 'FINAL SCHEDULE: ', schedule_s  # Python 2.x		

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

def coloring(graph):
	# Initialize structures
	result = {}
	V = graph.vertices()
	
	# Assign the first color to the first vertex
	result[V[0]] = 0

	# Initialize the remaining vetex without color
	for i in range(1,len(V)):
		result[V[i]] = -1
	
	# Temporary array to hold the available colors,
	# If it is false means the color cr is assigned to
	# one of its adjacent vertices
	available = {}
	for cr in range(0,len(V)):
		available[cr] = True
	
	# Assign color to remaining vertices
	for i in range(1,len(V)):
		# Process neighbours of Vertex v[i]
		for u in graph.vizinhos(V[i]):
			if result[u] != -1:
				available[result[u]] = False
	
		# Find the first available color
		color = -1
		for cr in range(0,len(V)):
			if available[cr] == True:
				color = cr
				break
		
		result[V[i]] = color

		# Reseting available colors for the next interation
		for u in graph.vizinhos(V[i]):
			if result[u] != -1:
				available[result[u]] = True
	
	return result


def dio(queue, degradation_limit, model_name):

	try:
		import time
		start = time.time()
		queue_aux = [(job[0],job[1][4]) for job in queue] #Using the predefined files #runtime,cycles,inst,cache_ref,cache_miss
		qList = sorted(queue_aux, key=lambda tup: tup[1])
		sched = []
		for i in range(0,len(qList)/2,1):
			f = len(qList)-1
			sched.append([qList[i][0],qList[f-i][0]])
		if (len(qList)%2): sched.append([qList[len(qList)/2][0]])
		end = time.time()

		with open('/tmp/SLURM_DIO_DEBUG.txt', 'a') as f:
			print >> f, 'qList: ', qList
			print >> f, 'FINAL SCHEDULE: ', sched
		f.closed
		return sched

	except Exception, e:
		with open('/tmp/SLURM_PYTHON_ERROR.txt', 'a') as f:
			print >> f, 'Filename:', type(e)  # Python 2.x
		f.closed


def shared(queue, degradation_limit, model_name):

	try:
		import time
		start = time.time()
		qList = [job[0] for job in queue]
		sched = []
		for i in range(0,len(qList),1):
			l = [qList[i]]
			for j in range(0,len(qList),1):
				if(i != j): l.append(qList[j])

			sched.append(l)
		end = time.time()

		with open('/tmp/SLURM_SHARED_DEBUG.txt', 'a') as f:
			print >> f, 'FINAL SCHEDULE: ', sched
		f.closed
		return sched

	except Exception, e:
		with open('/tmp/SLURM_SHARED_ERROR.txt', 'a') as f:
			print >> f, 'Filename:', type(e)  # Python 2.x
		f.closed


#list = [(100,[9453421259.7016,13291929505.8099,2762214.56838366,1708659.15630551,265.433392539964,1.23978685612789,182.914742451155,5325777527.32504,1777569116.47425,6453480.92539964,7124217.56483126,272316.511545293,9009348.9982238,1547883873.84902,4964084923.23446,1169331451.42096,2868079442.14742,18476.8703374778,174416.003552398,8578157.12078153,2188099.02486678,6556159.1616341,1001969.57193606,10201175.8010657,1997350.19182948,10270883.7442274,10233890.9271758,1682471.21314387,2161747.9946714,12091253.5488455,12091244.6749556,2868976462.45115,5586663.04973357,2162140.54884547,1.63622288517274,55.8568738942929,0.000239009239475857,438.801065719361,0.000130229253128051,41652276,65370793,103979,34347,0,0,0,15858771,10440338,8415,33227,9340,106701,13065289,1754474,10454778,14414368,56,845,48571,32252,19054,20440,188018,59758,138347,186198,6931,196987,255052,255032,11722384,153085,196936,1.3200453365693,29.5648798938663,0.000151107340328694,438,4.50799185129344e-05,17427060321,23364160110,11431560,6326357,3361,32,597,10504579420,3388373062,13359909,14552354,4983028,23221553,2257002251,10306278684,1871139749,4984822504,166257,899890,17724780,4315840,13375821,2230645,21690732,5051179,21392697,22121539,9510494,9934294,25219740,25219506,4987452705,15362121,9935181,2.1224747785242,70.6154014068229,0.00214052780421373,441,0.00107626212069239,7370106033.41929,9218544855.6796,2004588.77620598,1317614.34301768,502.503154594691,3.70500977912144,187.067964449284,4797933637.13146,1454773400.44816,6385726.06461601,6779994.34600557,579694.301158129,4528501.03188986,649062538.201371,4915701315.27921,636592726.74461,1933823459.79903,22755.7317347101,134869.295071903,8056182.2853738,1768809.38730117,6153323.51725728,888758.635716618,8785261.18857136,1514263.97534495,9423948.66216762,8696753.70527064,1368232.15627912,1596917.22857895,10364367.3550649,10364368.3325495,1934004892.73039,4441146.11954651,1597077.52874568,0.337036420888208,11.2858381315706,0.000203481613663537,0.983601632689705,0.000103127124792303]),
#(110,[9453421259.7016,13291929505.8099,2762214.56838366,1708659.15630551,265.433392539964,1.23978685612789,182.914742451155,5325777527.32504,1777569116.47425,6453480.92539964,7124217.56483126,272316.511545293,9009348.9982238,1547883873.84902,4964084923.23446,1169331451.42096,2868079442.14742,18476.8703374778,174416.003552398,8578157.12078153,2188099.02486678,6556159.1616341,1001969.57193606,10201175.8010657,1997350.19182948,10270883.7442274,10233890.9271758,1682471.21314387,2161747.9946714,12091253.5488455,12091244.6749556,2868976462.45115,5586663.04973357,2162140.54884547,1.63622288517274,55.8568738942929,0.000239009239475857,438.801065719361,0.000130229253128051,41652276,65370793,103979,34347,0,0,0,15858771,10440338,8415,33227,9340,106701,13065289,1754474,10454778,14414368,56,845,48571,32252,19054,20440,188018,59758,138347,186198,6931,196987,255052,255032,11722384,153085,196936,1.3200453365693,29.5648798938663,0.000151107340328694,438,4.50799185129344e-05,17427060321,23364160110,11431560,6326357,3361,32,597,10504579420,3388373062,13359909,14552354,4983028,23221553,2257002251,10306278684,1871139749,4984822504,166257,899890,17724780,4315840,13375821,2230645,21690732,5051179,21392697,22121539,9510494,9934294,25219740,25219506,4987452705,15362121,9935181,2.1224747785242,70.6154014068229,0.00214052780421373,441,0.00107626212069239,7370106033.41929,9218544855.6796,2004588.77620598,1317614.34301768,502.503154594691,3.70500977912144,187.067964449284,4797933637.13146,1454773400.44816,6385726.06461601,6779994.34600557,579694.301158129,4528501.03188986,649062538.201371,4915701315.27921,636592726.74461,1933823459.79903,22755.7317347101,134869.295071903,8056182.2853738,1768809.38730117,6153323.51725728,888758.635716618,8785261.18857136,1514263.97534495,9423948.66216762,8696753.70527064,1368232.15627912,1596917.22857895,10364367.3550649,10364368.3325495,1934004892.73039,4441146.11954651,1597077.52874568,0.337036420888208,11.2858381315706,0.000203481613663537,0.983601632689705,0.000103127124792303]),
#(120,[14425633578.6564,13296282669.8769,31695411.3504274,13042239.6598291,453.569230769231,69.5316239316239,51737.4017094017,9884440532.29573,5710580979.06667,7917098.55897436,21327125.4564103,541123.565811966,26472512.3111111,1795958522.56752,6547704278.27521,2913421807.7812,4496816669.7453,122132.165811966,2084413.44786325,316137835.967521,19316292.0615385,20994666.5213675,1259565.53504274,44654801.9418803,28976706.3076923,337268064.083761,54021456.442735,5302186.98803419,9703465.13846154,347494393.558974,347489197.576068,4495555907.48547,133610112.326496,9965824.18974359,0.844622800813496,37.3001942609352,0.00631375686080462,700.810256410256,0.00292285351329909,197868054,71479268,1626924,1351151,0,0,4,173492374,147744171,53558,97708,9125,12020,10721688,168780514,10135238,21195341,1401,25479,107790,65942,33722,73364,517897,641814,1596964,1757965,9121,1231537,1866309,1866216,21319070,1603529,1231307,0.24332485832836,16.4161873272811,0.000765563825013421,695,0.000154206573656644,17405233761,24163555678,71898420,45911114,5267,1386,136575,11587107372,10045498984,15746164,30603941,8865453,37234988,2974097292,10438258087,5467507530,8414668023,1392333,4027079,647655231,42878967,30571240,3674761,71680136,62989029,663147365,98053730,13260079,24663130,665371941,665371440,8419641472,267070713,24867811,1.38978291206259,90.9003640468306,0.022760781489816,705,0.0189026977724506,3195168966.76833,9618642820.62575,12806859.7728337,8959886.48622588,650.087195740181,123.706026260429,58349.0984750436,1669452881.29797,2883845064.46217,3191872.46949805,4354771.41565195,1135726.90627552,9080369.29703175,1034763521.45195,3183545323.99005,2261737242.50577,3481376144.2629,181114.264041628,894314.531530249,287766519.300923,11683385.5875704,4505924.70970015,707507.787298086,12765178.0173296,12302958.857769,285902571.962207,15354191.3503349,4353832.39100861,3424373.12390633,278609506.324088,278609541.117393,3486139639.84103,114893958.908121,3360112.33594676,0.516282904196657,15.8436821802087,0.00604866289476158,3.05820770255214,0.00300270506876415]),
#(200,[9453421259.7016,13291929505.8099,2762214.56838366,1708659.15630551,265.433392539964,1.23978685612789,182.914742451155,5325777527.32504,1777569116.47425,6453480.92539964,7124217.56483126,272316.511545293,9009348.9982238,1547883873.84902,4964084923.23446,1169331451.42096,2868079442.14742,18476.8703374778,174416.003552398,8578157.12078153,2188099.02486678,6556159.1616341,1001969.57193606,10201175.8010657,1997350.19182948,10270883.7442274,10233890.9271758,1682471.21314387,2161747.9946714,12091253.5488455,12091244.6749556,2868976462.45115,5586663.04973357,2162140.54884547,1.63622288517274,55.8568738942929,0.000239009239475857,438.801065719361,0.000130229253128051,41652276,65370793,103979,34347,0,0,0,15858771,10440338,8415,33227,9340,106701,13065289,1754474,10454778,14414368,56,845,48571,32252,19054,20440,188018,59758,138347,186198,6931,196987,255052,255032,11722384,153085,196936,1.3200453365693,29.5648798938663,0.000151107340328694,438,4.50799185129344e-05,17427060321,23364160110,11431560,6326357,3361,32,597,10504579420,3388373062,13359909,14552354,4983028,23221553,2257002251,10306278684,1871139749,4984822504,166257,899890,17724780,4315840,13375821,2230645,21690732,5051179,21392697,22121539,9510494,9934294,25219740,25219506,4987452705,15362121,9935181,2.1224747785242,70.6154014068229,0.00214052780421373,441,0.00107626212069239,7370106033.41929,9218544855.6796,2004588.77620598,1317614.34301768,502.503154594691,3.70500977912144,187.067964449284,4797933637.13146,1454773400.44816,6385726.06461601,6779994.34600557,579694.301158129,4528501.03188986,649062538.201371,4915701315.27921,636592726.74461,1933823459.79903,22755.7317347101,134869.295071903,8056182.2853738,1768809.38730117,6153323.51725728,888758.635716618,8785261.18857136,1514263.97534495,9423948.66216762,8696753.70527064,1368232.15627912,1596917.22857895,10364367.3550649,10364368.3325495,1934004892.73039,4441146.11954651,1597077.52874568,0.337036420888208,11.2858381315706,0.000203481613663537,0.983601632689705,0.000103127124792303]),
#(210,[9453421259.7016,13291929505.8099,2762214.56838366,1708659.15630551,265.433392539964,1.23978685612789,182.914742451155,5325777527.32504,1777569116.47425,6453480.92539964,7124217.56483126,272316.511545293,9009348.9982238,1547883873.84902,4964084923.23446,1169331451.42096,2868079442.14742,18476.8703374778,174416.003552398,8578157.12078153,2188099.02486678,6556159.1616341,1001969.57193606,10201175.8010657,1997350.19182948,10270883.7442274,10233890.9271758,1682471.21314387,2161747.9946714,12091253.5488455,12091244.6749556,2868976462.45115,5586663.04973357,2162140.54884547,1.63622288517274,55.8568738942929,0.000239009239475857,438.801065719361,0.000130229253128051,41652276,65370793,103979,34347,0,0,0,15858771,10440338,8415,33227,9340,106701,13065289,1754474,10454778,14414368,56,845,48571,32252,19054,20440,188018,59758,138347,186198,6931,196987,255052,255032,11722384,153085,196936,1.3200453365693,29.5648798938663,0.000151107340328694,438,4.50799185129344e-05,17427060321,23364160110,11431560,6326357,3361,32,597,10504579420,3388373062,13359909,14552354,4983028,23221553,2257002251,10306278684,1871139749,4984822504,166257,899890,17724780,4315840,13375821,2230645,21690732,5051179,21392697,22121539,9510494,9934294,25219740,25219506,4987452705,15362121,9935181,2.1224747785242,70.6154014068229,0.00214052780421373,441,0.00107626212069239,7370106033.41929,9218544855.6796,2004588.77620598,1317614.34301768,502.503154594691,3.70500977912144,187.067964449284,4797933637.13146,1454773400.44816,6385726.06461601,6779994.34600557,579694.301158129,4528501.03188986,649062538.201371,4915701315.27921,636592726.74461,1933823459.79903,22755.7317347101,134869.295071903,8056182.2853738,1768809.38730117,6153323.51725728,888758.635716618,8785261.18857136,1514263.97534495,9423948.66216762,8696753.70527064,1368232.15627912,1596917.22857895,10364367.3550649,10364368.3325495,1934004892.73039,4441146.11954651,1597077.52874568,0.337036420888208,11.2858381315706,0.000203481613663537,0.983601632689705,0.000103127124792303]),
#(220,[14425633578.6564,13296282669.8769,31695411.3504274,13042239.6598291,453.569230769231,69.5316239316239,51737.4017094017,9884440532.29573,5710580979.06667,7917098.55897436,21327125.4564103,541123.565811966,26472512.3111111,1795958522.56752,6547704278.27521,2913421807.7812,4496816669.7453,122132.165811966,2084413.44786325,316137835.967521,19316292.0615385,20994666.5213675,1259565.53504274,44654801.9418803,28976706.3076923,337268064.083761,54021456.442735,5302186.98803419,9703465.13846154,347494393.558974,347489197.576068,4495555907.48547,133610112.326496,9965824.18974359,0.844622800813496,37.3001942609352,0.00631375686080462,700.810256410256,0.00292285351329909,197868054,71479268,1626924,1351151,0,0,4,173492374,147744171,53558,97708,9125,12020,10721688,168780514,10135238,21195341,1401,25479,107790,65942,33722,73364,517897,641814,1596964,1757965,9121,1231537,1866309,1866216,21319070,1603529,1231307,0.24332485832836,16.4161873272811,0.000765563825013421,695,0.000154206573656644,17405233761,24163555678,71898420,45911114,5267,1386,136575,11587107372,10045498984,15746164,30603941,8865453,37234988,2974097292,10438258087,5467507530,8414668023,1392333,4027079,647655231,42878967,30571240,3674761,71680136,62989029,663147365,98053730,13260079,24663130,665371941,665371440,8419641472,267070713,24867811,1.38978291206259,90.9003640468306,0.022760781489816,705,0.0189026977724506,3195168966.76833,9618642820.62575,12806859.7728337,8959886.48622588,650.087195740181,123.706026260429,58349.0984750436,1669452881.29797,2883845064.46217,3191872.46949805,4354771.41565195,1135726.90627552,9080369.29703175,1034763521.45195,3183545323.99005,2261737242.50577,3481376144.2629,181114.264041628,894314.531530249,287766519.300923,11683385.5875704,4505924.70970015,707507.787298086,12765178.0173296,12302958.857769,285902571.962207,15354191.3503349,4353832.39100861,3424373.12390633,278609506.324088,278609541.117393,3486139639.84103,114893958.908121,3360112.33594676,0.516282904196657,15.8436821802087,0.00604866289476158,3.05820770255214,0.00300270506876415])]
#
##ml = colocation_pairs(list,100)
#ml = shared(list,100,"")
#print("Model schedule = {r1}".format(r1=ml))
### RESULT TESTE: [[jobid,jobid],[jobid]...] Model schedule = [[100, 110], [120], [140], [150, 190], [160], [180]]
