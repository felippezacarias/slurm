import sys, os
import pickle
from degradation_model import colocation_graph, colocation_graph_detailed, colocation_pairs, colocation_pairs_optimal

def try_to_assign(jobs, nodes):
	import copy
	result = []
	def assign(jobs, nnodes):
		njobs = []
		for job in jobs:
			a = False
			nnodes = sorted(nnodes, key=lambda x : -len(x[1]))
			for n in nnodes:
				if len(n[1])<n[0]:
					a = True
					for j in job[1]:
						if j in n[1]:
							a = False
							break
					if a:
						n[1].add(job[0])
						break
			if not a:
				njobs.append(job)
		return njobs
	l = len(jobs)
	while l>0:
		nnodes = copy.deepcopy(nodes)
		jobs = assign(jobs, nnodes)
		if l==len(jobs):
			jobs.pop(0)
			result.append([])
			
		result.append(nnodes)
		l = len(jobs)
	return result
	
	
def try_to_assign_detailed(jobs, nodes, degradation_limit):
	import copy
	result = []
	def degradation_cost(jobs):
		t = 0
		for j in jobs:
			t += j[1]
		return t
	def assign(jobs, nnodes, degradation_limit):
		njobs = []
		ns = {}
		for i in range(len(nnodes)):
			ns[i] = []
		for job in jobs:
			a = False
			nnodes = sorted(nnodes, key=lambda x : -len(x[1]))
			i = 0
			for n in nnodes:
				if len(n[1])<n[0]:
					a = True
					c = 0
					for j in job[1].keys():
						if j in n[1]:
							c = job[1][j]
							if job[1][j]>degradation_limit:
								a = False
								break
					if a:
						n[1].add(job[0])
						ns[i].append((job[0], c))
						break
				i += 1
			if not a:
				njobs.append(job)
		return (njobs, ns)
	l = len(jobs)
	cost = 0.0
	while l>0:
		nnodes = copy.deepcopy(nodes)
		jobs, ns = assign(jobs, nnodes, degradation_limit)
		for n in ns.keys():
			cost = max(degradation_cost(ns[n]), cost)
		if l==len(jobs):
			jobs.pop(0)
			result.append([])
		result.append(nnodes)
		l = len(jobs)
	return (result, cost)
	
def find_minimal_assignment_combinatorial(jobs, nodes):
	from itertools import permutations
	first = True
	minimal = 0
	jobs_result = None
	nodes_result = None
	for jl in permutations(jobs):
		res = try_to_assign(jl, nodes)
		if first or len(res)<minimal:
			minimal = len(res)
			nodes_result = res
			jobs_result = jl
			first = False
	return (jobs_result, nodes_result)
	
def find_minimal_assignment_detailed_combinatorial(jobs, nodes, degradation_limit):
	from itertools import permutations
	first = True
	minimal = 0
	minimal_cost = 0
	jobs_result = None
	nodes_result = None
	for jl in permutations(jobs):
		res, cost = try_to_assign_detailed(jl, nodes, degradation_limit)
		if first or (len(res)<minimal or (len(res)==minimal and cost<minimal_cost)):
			minimal = len(res)
			minimal_cost = cost
			nodes_result = res
			jobs_result = jl
			first = False
	return (jobs_result, nodes_result)

def find_minimal_assignment(jobs, nodes, degradation_limit):
	nnodes = []
	for n in nodes:
		nnodes.append((n[0], set(n[1])))
	jobs_result, nodes_result = find_minimal_assignment_combinatorial(jobs, nnodes)
	'''for job in jobs_result:
		flist = []
		for j in job[1]:
			flist.append((j, 2.0))
		job[1] = flist'''
	
	return (list(jobs_result), nodes_result)

def find_minimal_assignment_detailed(jobs, nodes, degradation_limit):
	nnodes = []
	for n in nodes:
		nnodes.append((n[0], set(n[1])))
	jobs_result, nodes_result = find_minimal_assignment_detailed_combinatorial(jobs, nnodes, degradation_limit)
	for job in jobs_result:
		flist = []
		for j in job[1].keys():
			#flist.append((j, job[1][j]))
			if job[1][j]>degradation_limit:
				flist.append(j)
		job[1] = flist
	return (list(jobs_result), nodes_result)