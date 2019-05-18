#!/bin/bash 
#SBATCH --job-name=slurm
#SBATCH -D .
#SBATCH --output=test_install-%j.out
#SBATCH -N 2
#SBATCH --queue=normal

date
#load proper modules
module load gcc/7.1.0 openmpi/1.10.4-apps
${slurm_install_dir}/slurm_programs/bin/srun hostname
date
