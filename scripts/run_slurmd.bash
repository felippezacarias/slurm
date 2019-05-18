#!/bin/bash 

#slurmd command
##################################################################
## This script is intended to start slurmd daemon in a node when 
## this slurm version executes as a job in a supercomputer environment.
## To prevent the job finalizing the user application in the node when
## the slurmd call returns, there is a sleep call at the end to keep the
## slurmd process alive during the batch execution.
##################################################################

$1
sleep $2 
