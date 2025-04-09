import sys, os
import pickle
from degradation_model import colocation_graph, colocation_graph_reordered, colocation_graph_reordered_detailed, colocation_graph_detailed, colocation_pairs, colocation_pairs_optimal
import job_assignment

jobs = []
with open("/home/counters_cpu/barnes.csv", "r") as f:
	jobs.append((2, [float(x) for x in f.read().split(",")], 0))

with open("/home/counters_cpu/blackscholes.csv", "r") as f:
	jobs.append((3, [float(x) for x in f.read().split(",")], 0))

with open("/home/counters_cpu/cmandel.csv", "r") as f:
	jobs.append((4, [float(x) for x in f.read().split(",")], 0))
	
with open("/home/counters_cpu/waterspatial.csv", "r") as f:
	jobs.append((5, [float(x) for x in f.read().split(",")], 0))

with open("/home/counters_cpu/kmeans.csv", "r") as f:
	jobs.append((6, [float(x) for x in f.read().split(",")], 0))

with open("/home/counters_cpu/fluidanimate.csv", "r") as f:
	jobs.append((7, [float(x) for x in f.read().split(",")], 0))

with open("/home/counters_cpu/hop.csv", "r") as f:
	jobs.append((8, [float(x) for x in f.read().split(",")], 0))

with open("/home/counters_cpu/ocean.csv", "r") as f:
	jobs.append((9, [float(x) for x in f.read().split(",")], 0))

    
nodes = [(5, [10,11,12]), (3, [13,14])]
nodes = [(2, []), (2, []), (2, []), (2, [])]
	
njobs = colocation_graph(jobs, None, 100)
print(njobs)
print('')
njobs = colocation_graph_reordered(jobs, nodes, 100)
print(njobs)
print('')
njobs = colocation_graph_reordered_detailed(jobs, nodes, 100)
print(njobs)
print('')
exit(0)

#njobs = [[2, [3, 4, 5, 6, 7, 8, 9]], [3, [2]], [4, [2, 7]], [5, [2]]]
njobs = [[1, [2, 3, 5]], [2, [1, 4, 5]], [3, [1, 4, 5]], [4, [2, 3, 5]], [5, [1,2,3,4]]]

#njobs = [[1, [2]], [2, [1]], [3, [4]], [4, [3]], [5, [1,2,3,4]]]
#njobs = [[1, [2,3]], [2, [1,3]], [3, [1,2]]]

nodes = [(5, [10,11,12]), (3, [13,14])]
nodes = [(6, []), (6, []), (6, [])]

result = job_assignment.find_minimal_assignment(njobs, nodes, 100)
print(result[0])
for r in result[1]:
	print(r)

print('')
print('')

njobs = [
	[1, {2:110, 3:110, 5:110, 4:90}],
	[2, {1:110, 3:90, 4:110, 5:110}],
	[3, {1:110, 2:90, 4:110, 5:110}],
	[4, {1:90, 2:110, 3:110, 5:110}],
	[5, {1:110, 2:110, 3:110, 4:110}]
	]
'''njobs = [
	[1, {2:110, 3:110, 5:110}],
	[2, {1:110, 5:110}],
	[3, {1:110}],
	[4, {}],
	[5, {1:110, 2:110}]
	]'''
nodes = [(5, [10,11,12]), (3, [13,14])]
nodes = [(4, []), (4, [])]

result = job_assignment.find_minimal_assignment_detailed(njobs, nodes, 200)
print(result[0])
for r in result[1]:
	print(r)

