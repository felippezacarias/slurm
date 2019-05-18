#!/bin/bash 
#SBATCH --job-name=slurm
#SBATCH -D .
#SBATCH --output=test_install-%j.out
#SBATCH -N 1

date
sleep $1
#/usr/bin/time $1
date
